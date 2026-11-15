// MOD1 batch B (docs/modern-assets-brief.md): large enterable modern buildings. A school, a hospital with a roof
// helipad, a leisure centre with a sunken pool, a big-box store with a loading dock, a two-platform train station and four
// office towers, plus the cart corral the store brings as a companion. Written by tools/build_mpu_assets.mjs.
//
// Every building is one or more rectangular wings (`wing`): a concrete plinth, floor slabs with ceilings, two-layer
// walls (cladding outside, plaster inside, parts.mjs wall()) with real openings, ribbon glazing whose units are window
// interactables (an opening sash each, so the auto-furnisher keeps wall items off the glass), doors, parapets and roof
// plant. Floors are planned as a corridor with a band of rooms either side (`dl`) or as explicit halls. Stair cores are
// the house() switchback (catalog_chateau.mjs) with solid precast steps, lift shafts carry doors at every stop, and a core
// marked `roof` rises into a bulkhead with a door onto the roof. Rooms carry A.meta.roomTypes for the furnisher; only
// fixed fittings (counters, bleachers, lockers, gondolas, checkouts, ticket barriers, the pool) are modelled here.
import { Asset, cutRect, wbox, P, swingSign } from "./kit.mjs";
import { wall, partition, hinged, aabb, fbox, railing } from "./parts.mjs";
import { mats as baseMats, comp } from "./catalog_chateau.mjs";
import { cover, fire, room, corners, setNav, setTour } from "./catalog_atlantic.mjs";

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
  A.mat("precast", [0.60, 0.59, 0.55], 0.82, "concrete", { texture: "concrete_aggregate" });
  A.mat("render_w", [0.74, 0.73, 0.70], 0.9, "plaster", { texture: "render_lime" });
  A.mat("brick_grey", [0.33, 0.32, 0.30], 0.85, "brick", { texture: "brick_muted" });
  A.mat("stone_clad", [0.62, 0.58, 0.50], 0.7, "stone", { texture: "limestone_smooth" });
  A.mat("panel_lt", [0.62, 0.63, 0.63], 0.45, "metal", { metallic: 0.2 });
  A.mat("panel_dk", [0.07, 0.08, 0.09], 0.4, "metal", { metallic: 0.3 });
  A.mat("panel_acc", accent, 0.45, "metal");
  A.mat("corrugated", [0.48, 0.50, 0.51], 0.45, "metal", { texture: "corrugated_galv", metallic: 0.7 });
  A.mat("alu", [0.20, 0.21, 0.22], 0.35, "metal", { metallic: 0.8 });
  A.mat("alu_lt", [0.55, 0.56, 0.57], 0.3, "metal", { metallic: 0.9 });
  A.mat("bronze", [0.20, 0.14, 0.09], 0.35, "metal", { metallic: 0.8 });
  A.mat("glass_t", [0.30, 0.42, 0.46], 0.04, "glass");
  A.mat("glass_int", [0.75, 0.80, 0.80], 0.15, "glass");
  A.mat("ceiling", [0.74, 0.74, 0.72], 0.9, "tile", { texture: "ceiling_backrooms" });
  A.mat("light_panel", [0.9, 0.9, 0.86], 0.5, "plain", { emissive: [2.2, 2.15, 2.0] });
  A.mat("roof_mem", [0.17, 0.17, 0.18], 0.85, "concrete", { texture: "concrete_stained" });
  A.mat("gravel", [0.42, 0.40, 0.37], 0.95, "stone", { texture: "gravel_fine" });
  A.mat("paving", [0.45, 0.44, 0.41], 0.85, "stone", { texture: "paving_worn" });
  A.mat("asphalt", [0.08, 0.08, 0.085], 0.92, "concrete", { texture: "concrete_rough" });
  A.mat("ballast", [0.30, 0.28, 0.26], 0.95, "stone", { texture: "gravel_coarse" });
  A.mat("rubber", [0.04, 0.04, 0.045], 0.9, "plain");
  A.mat("lift_steel", [0.62, 0.62, 0.63], 0.3, "metal", { texture: "steel_scratched", metallic: 1.0 });
  A.mat("rail_steel", [0.42, 0.40, 0.38], 0.4, "metal", { texture: "steel_rust_light", metallic: 0.9 });
  A.mat("safety_yel", [0.62, 0.48, 0.05], 0.6, "metal");
  A.mat("orange", [0.62, 0.18, 0.03], 0.6, "metal");
  A.mat("paint_w", [0.80, 0.80, 0.78], 0.6, "metal");
  A.mat("hvac", [0.55, 0.56, 0.55], 0.5, "metal", { metallic: 0.3 });
  A.mat("louvre", [0.25, 0.26, 0.27], 0.5, "metal", { metallic: 0.4 });
  A.mat("laminate", [0.55, 0.53, 0.48], 0.5, "wood", { texture: "wood_oak" });
  A.mat("sign_blank", accent.map((v) => v * 0.8), 0.5, "plain", { emissive: accent.map((v) => v * 0.6) });
  A.mat("lamp_g", [0.1, 0.5, 0.15], 0.4, "plain", { emissive: [0.3, 2.0, 0.5] });
  A.mat("screen", [0.02, 0.03, 0.04], 0.2, "plain", { emissive: [0.25, 0.45, 0.6] });
  A.mat("water_proxy", [0.05, 0.1, 0.1], 0.1, "glass");
  A.mat("pool_tile", [0.18, 0.42, 0.52], 0.25, "tile", { texture: "pooltile_backrooms" });
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
function finish(A, cuts = null) { setNav(A, A._nav, cuts); delete A._nav; return A; }
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
  WB(A, F, a0, a0 + fw, floor, top - fw, depth - 0.2, depth - 0.02, "alu");
  WB(A, F, a1 - fw, a1, floor, top - fw, depth - 0.2, depth - 0.02, "alu");
  WB(A, F, a0, a1, top - fw, top, depth - 0.2, depth - 0.02, "alu");
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
  const w = { T: 0.3, gf: GF, omit: [], n: 1, base: true, par: 1.1, cores: [], lifts: [], doors: [], links: [], special: [], band: "panel_dk", ...cfg };
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
        for (const [p0, p1] of [[a0 - 0.05, a0 + 0.01], [a1 - 0.01, a1 + 0.05]]) WB(A, F, p0, p1, y, lh - 0.01, -0.02, w.T + 0.02, "alu", L1);
        WB(A, F, a0 - 0.05, a1 + 0.05, lh - 0.01, lh + 0.05, -0.02, w.T + 0.02, "alu", L1);
        navDoor(A, l.a, l.b, F.axis === "x" ? [l.c, y, F.outer - F.out * w.T / 2] : [F.outer - F.out * w.T / 2, y, l.c]);
      }
      const G = w.glaze?.(f)?.[s] ?? [];
      G.forEach((g, i) => H.push(ribbon(A, F, { id: `${w.tag}${s}${f}_${i}`, a0: g[0], a1: g[1], y0: y + g[2], y1: y + g[3], unit: g[4] ?? w.unit ?? 3.6, floor: y, fin: w.fin ?? 0.16, finMat: w.finMat, glass: w.glass })));
      wall(A, { axis: F.axis, outer: F.outer, inner: F.inner, aOut: F.aOut, aIn: F.aIn, y0: yb, y1: yt, holes: H, stone: w.skin(f, s), plaster: w.plaster ?? "plaster", pt: PT });
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
    wbox(A, p.axis, q.c - q.w / 2 - 0.06, q.c + q.w / 2 + 0.06, hh - 0.01, hh + 0.06, p.d - t / 2 - 0.015, p.d + t / 2 + 0.015, "alu", L1);
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
// basketball backboard on a wall face at x = xw, facing s (+1 / -1 along x)
function backboard(A, xw, s, zc, y) {
  A.box([Math.min(xw, xw + s * 1.2), y + 3.2, zc - 0.08], [Math.max(xw, xw + s * 1.2), y + 3.3, zc + 0.08], "steel_plate", L1);
  A.box([Math.min(xw + s * 1.2, xw + s * 1.24), y + 2.9, zc - 0.9], [Math.max(xw + s * 1.2, xw + s * 1.24), y + 3.95, zc + 0.9], "glass_int", K0);
  A.box([Math.min(xw + s * 1.19, xw + s * 1.25), y + 3.05, zc - 0.3], [Math.max(xw + s * 1.19, xw + s * 1.25), y + 3.5, zc - 0.27], "paint_w", L1);
  A.cyl([xw + s * 1.5, y + 3.05, zc], [0.46, 0.02, 0.46], "orange", { k: 0, seg: 16 });
}
function courtLines(A, x0, x1, z0, z1, y) {
  const L = (a, b, c, d) => A.box([a, y, c], [b, y + 0.007, d], "paint_w", L1);
  L(x0, x1, z0, z0 + 0.05); L(x0, x1, z1 - 0.05, z1); L(x0, x0 + 0.05, z0 + 0.05, z1 - 0.05); L(x1 - 0.05, x1, z0 + 0.05, z1 - 0.05);
  L((x0 + x1) / 2 - 0.025, (x0 + x1) / 2 + 0.025, z0 + 0.05, z1 - 0.05);
  A.cyl([(x0 + x1) / 2, y + 0.002, (z0 + z1) / 2], [3.6, 0.003, 3.6], "panel_acc", { k: 0, lod: 1, seg: 24 });
}
function trusses(A, x0, x1, z0, z1, y, sp = 4.5, alongZ = true) {
  if (alongZ) for (let x = x0 + sp / 2; x < x1 - 0.5; x += sp) { A.box([x - 0.1, y - 0.7, z0], [x + 0.1, y - 0.62, z1], "panel_dk", L1); A.box([x - 0.1, y - 0.08, z0], [x + 0.1, y, z1], "panel_dk", L1); A.box([x - 0.02, y - 0.62, z0], [x + 0.02, y - 0.08, z1], "panel_dk", L1); }
  else for (let z = z0 + sp / 2; z < z1 - 0.5; z += sp) { A.box([x0, y - 0.7, z - 0.1], [x1, y - 0.62, z + 0.1], "panel_dk", L1); A.box([x0, y - 0.08, z - 0.1], [x1, y, z + 0.1], "panel_dk", L1); A.box([x0, y - 0.62, z - 0.02], [x1, y - 0.08, z + 0.02], "panel_dk", L1); }
}
function bleachers(A, x0, x1, zWall, s, y, tiers = 3, dz = 0.8, dy = 0.45) {
  for (let i = 0; i < tiers; i++) {
    const za = zWall - s * (tiers - i) * dz;
    A.box([x0, y + i * dy, Math.min(za, zWall)], [x1, y + (i + 1) * dy, Math.max(za, zWall)], "precast");
    A.box([x0 + 0.05, y + (i + 1) * dy, Math.min(za, za + s * 0.45)], [x1 - 0.05, y + (i + 1) * dy + 0.04, Math.max(za, za + s * 0.45)], "panel_acc", L1);   // seat boards
  }
}
function lockers(A, x0, x1, z, s, y) {   // a bank of lockers against a wall at z, fronts facing s
  A.box([x0, y, Math.min(z, z + s * 0.5)], [x1, y + 1.9, Math.max(z, z + s * 0.5)], "panel_acc", { bv: 0.01 });
  for (let x = x0 + 0.4; x < x1 - 0.1; x += 0.4) A.box([x - 0.006, y + 0.05, Math.min(z + s * 0.5, z + s * 0.51)], [x + 0.006, y + 1.85, Math.max(z + s * 0.5, z + s * 0.51)], "panel_dk", L1);
  A.box([x0, y, Math.min(z + s * 0.8, z + s * 1.15)], [x1, y + 0.45, Math.max(z + s * 0.8, z + s * 1.15)], "laminate", { bv: 0.01 });   // bench
}
function tour(A, start, steps) { setTour(A, { pos: start, yaw: 0 }, steps); }
function doorTour(A, id, x, zOut, zIn, gf, shot) {
  tour(A, [x, 0, zOut + 7], [{ goto: [x, zOut + 1.2], note: "approach the entrance" }, { interact: id, aim: [x + 0.3, gf + 1.1, zIn + 0.02], expectOpen: true },
    { goto: [x, zIn - 2.0], expectY: gf, note: "into the lobby" }, { look: [x, gf + 1.4, zIn - 8], hold: 0.5 }, { shot }]);
}

// ============================================================================ 1. school
export function school() {
  const A = mk("mod_bld_school", "Modern school", "Two-storey school, 36 x 16 m classroom block of brick with a white-rendered upper floor and ribbon windows, a cantilevered entrance canopy, plus a 20 x 16 m sports hall in ribbed metal cladding with clerestory glazing and a single-storey brick cafeteria behind. Inside: an entrance lobby, offices, a staff room, eleven classrooms off a central corridor on two floors, two stair cores and a lift, toilets on both floors; the gym with court markings, backboards, wall bars and bleachers; the dining hall with a servery hatch and a kitchen.",
    2, [57, 29], [0.10, 0.34, 0.20]);
  const ys = [GF, GF + 3.6];
  const kW = mkCore({ xa: -21.7, zA: 1.32, ys, encR: true, encTo: 7.7, tag: "core_w" });
  const kE = mkCore({ xa: 11.08, zA: 1.32, ys, encL: true, encR: false, encTo: 7.7, tag: "core_e" });
  const lift = mkLift({ x0: -21.7, x1: -19.3, z0: -7.7, z1: -1.2, door: "front", ys });
  const zc = [-1.2, 1.2];
  const wa = wing(A, {
    tag: "a", x0: -22, x1: 14, z0: -8, z1: 8, n: 2, h: 3.6, cores: [kW, kE], lifts: [lift], band: "panel_acc",
    skin: (f) => (f === 0 ? "brick" : "render_w"),
    doors: [{ id: "main_entrance", side: "front", c: -11, w: 2.0, h: 2.4, double: true, glass: true, room: "a0_lobby" },
      { id: "west_exit", side: "west", c: 0, w: 1.0, room: "a0_corridor" }],
    links: [{ side: "east", c: 0, w: 2.2, h: 2.5, a: "a0_corridor", b: "gym" }, { side: "rear", c: -4, w: 2.0, h: 2.5, a: "a0_passage", b: "dining" }],
    glaze: (f) => (f === 0 ? { front: [[-15.4, -12.3, 0.1, 3.0], [-9.7, -6.4, 0.1, 3.0], [-5.6, 10.5, 0.9, 3.0, 3.2]], rear: [[-19.0, -5.4, 0.9, 3.0, 3.4], [-2.6, 13.2, 0.9, 3.0, 3.2]] }
      : { front: [[-15.5, 10.6, 0.9, 3.0, 3.25]], rear: [[-19.0, 13.2, 0.9, 3.0, 3.4]], west: [[-1.0, 1.0, 0.9, 3.0]] }),
    plan: (f, { w }) => (f === 0 ? dl(w, { pre: "a0_", zc,
      front: [{ x1: -18.93, skip: 1 }, { x1: -15.8, id: "wc", type: "bathroom" }, { x1: -6.0, id: "lobby", type: "lobby", open: 1 }, { x1: 2.4, id: "office_a", type: "office" }, { x1: 10.93, id: "office_b", type: "office" }, { skip: 1 }],
      rear: [{ x1: -19.3, skip: 1 }, { x1: -12, id: "class_1", type: "class" }, { x1: -5.5, id: "class_2", type: "class" }, { x1: -2.5, id: "passage", type: "hall", open: 1 }, { x1: 4.5, id: "class_3", type: "class" }, { x1: 9, id: "staff", type: "office" }, { id: "store", type: "utility" }] })
      : dl(w, { pre: "a1_", zc,
        front: [{ x1: -18.93, skip: 1 }, { x1: -15.8, id: "wc", type: "bathroom" }, { x1: -8.5, id: "class_4", type: "class" }, { x1: -1, id: "class_5", type: "class" }, { x1: 5, id: "class_6", type: "class" }, { x1: 10.93, id: "class_7", type: "class" }, { skip: 1 }],
        rear: [{ x1: -19.3, skip: 1 }, { x1: -12, id: "class_8", type: "class" }, { x1: -4, id: "class_9", type: "class" }, { x1: 4.5, id: "class_10", type: "class" }, { id: "class_11", type: "class" }] })),
    roof: (A, w) => { hvac(A, -2, -4.5, w.top); hvac(A, 2.5, -4.5, w.top); plantScreen(A, -3.8, 4.3, -6.4, -2.6, w.top, 1.7); },
  });
  for (const k of [kW, kE]) buildCore(A, k, wa.top - SLAB, ["a0_corridor", "a1_corridor"]);
  buildLift(A, lift, wa.top);
  // sports hall (east) and cafeteria (behind), lower than the classroom block
  wing(A, {
    tag: "b", x0: 14, x1: 34, z0: -8, z1: 8, n: 1, h: 7.0, omit: ["west"], fm: "plank_x", band: null, skin: () => "corrugated",
    doors: [{ id: "gym_exit", side: "east", c: 0, w: 1.8, double: true, room: "gym" }],
    glaze: () => ({ front: [[15, 33, 4.6, 6.4, 3.6]], rear: [[15, 33, 4.6, 6.4, 3.6]] }),
    plan: () => ({ rooms: [{ id: "gym", type: "gym", x0: 14, z0: -7.7, x1: 33.7, z1: 7.7 }], walls: [] }),
    dress: (A, w) => {
      backboard(A, 14, 1, 0, GF); backboard(A, 33.7, -1, 0, GF);
      courtLines(A, 15.2, 32.5, -6.9, 6.9, GF);
      bleachers(A, 18, 30, 7.7, 1, GF);
      trusses(A, 14, 33.7, -7.7, 7.7, w.top - SLAB - 0.02, 4.0);
      for (const x of [20, 21.2]) A.box([x - 0.04, GF, -7.7], [x + 0.04, GF + 2.6, -7.6], "laminate", L1);   // wall bars
      for (let y = GF + 0.3; y < GF + 2.6; y += 0.3) A.box([20, y - 0.02, -7.62], [21.2, y + 0.02, -7.58], "laminate", L1);
      A.box([14.02, GF, -6], [14.06, GF + 2.0, 6], "panel_acc", L1);   // impact padding on the end walls
      A.box([33.64, GF, -6], [33.68, GF + 2.0, 6], "panel_acc", L1);
      for (let x = 15; x < 34; x += 4.75) A.box([x - 0.05, GF - 0.05, 8.0], [x + 0.05, w.top + 1.1, 8.075], "panel_dk", L1);   // cladding ribs
    },
  });
  wing(A, {
    tag: "c", x0: -14, x1: 2, z0: -20, z1: -8, n: 1, h: 4.0, omit: ["front"], skin: () => "brick", band: null,
    doors: [{ id: "dining_exit", side: "rear", c: -11, w: 1.8, double: true, glass: true, room: "dining" }, { id: "kitchen_door", side: "rear", c: -0.2, w: 1.0, room: "kitchen" }],
    glaze: () => ({ west: [[-19.2, -8.6, 0.9, 3.2, 3.5]], rear: [[-9.5, -4.0, 0.9, 3.2, 2.8]], east: [[-19.0, -15.0, 1.2, 2.6]] }),
    plan: () => ({ rooms: [{ id: "dining", type: "dining", x0: -13.7, z0: -19.7, x1: -3.06, z1: -8 }, { id: "kitchen", type: "prokitchen", x0: -2.94, z0: -19.7, x1: 1.7, z1: -8 }],
      walls: [{ axis: "z", d: -3.0, a0: -19.7, a1: -8, holes: [{ c: -10.0, w: 1.0, a: "dining", b: "kitchen" }, { c: -14.5, w: 2.4, y0: 0.9, h: 2.1 }] }] }),
    dress: (A) => {
      A.box([-3.4, GF, -15.7], [-2.6, GF + 0.9, -13.3], "laminate", { bv: 0.01 });          // servery counter in the hatch
      A.box([-3.45, GF + 0.9, -15.75], [-2.55, GF + 0.94, -13.25], "lift_steel", K0);
      A.box([-3.0, GF + 1.35, -15.6], [-2.8, GF + 1.38, -13.4], "lift_steel", L1);           // sneeze guard rail
      A.box([0.9, GF + 2.2, -18.8], [1.7, GF + 2.7, -15.2], "lift_steel", L1);               // extract hood
      A.box([1.1, GF + 2.7, -17.4], [1.5, GF + 3.98, -16.6], "lift_steel", L1);
    },
  });
  // entrance canopy, sign, plant
  canopy(A, { x0: -14.2, x1: -7.8, z0: 8, z1: 10.8, y: 3.35, cols: [[-13.7, 10.4], [-8.3, 10.4]] });
  fbox(A, sideF(wa, "front"), -13.9, -8.1, 3.75, 4.25, 0, 0.05, "sign_blank", L1);
  coverRing(A, -22, 34, -20, 8);
  doorTour(A, "main_entrance", -11, 8, 7.7, GF, "mod_school_a_lobby");
  return finish(A);
}

// ============================================================================ 2. hospital
export function hospital() {
  const A = mk("mod_bld_hospital", "Hospital", "Four-storey general hospital, 48 x 17 m: a dark brick ground floor under three floors of white panel cladding with long ribbon windows, an entrance canopy over a drop-off, and a helipad on the roof (raised pad with touchdown markings, edge lights, safety nets and a windsock) reached by a stair to a roof door. Inside: a double-height-glazed lobby with a reception desk, admissions and consulting rooms, an ambulance bay; on every floor wards either side of a wide corridor, a nurse station with its counter, offices, utility rooms and toilets; two stair cores and two bed lifts.",
    4, [48, 23], [0.05, 0.36, 0.42]);
  const h = 3.8, ys = [0, 1, 2, 3].map((f) => GF + f * h), roofY = GF + 4 * h, zc = [-1.3, 1.3];
  const kW = mkCore({ xa: -23.7, zA: 1.42, ys, encR: true, encTo: 8.2, tag: "core_w" });
  const kE = mkCore({ xa: 21.08, zA: 1.42, ys: [...ys, roofY], encL: true, encR: false, encTo: 8.2, roof: true, tag: "core_e" });
  const lw = mkLift({ x0: -23.7, x1: -20.9, z0: -8.2, z1: -1.3, door: "front", ys, tag: "lift_w" });
  const le = mkLift({ x0: 20.9, x1: 23.7, z0: -8.2, z1: -1.3, door: "front", ys, tag: "lift_e" });
  const w = wing(A, {
    tag: "h", x0: -24, x1: 24, z0: -8.5, z1: 8.5, n: 4, h, cores: [kW, kE], lifts: [lw, le], band: "panel_acc", finMat: "alu_lt",
    skin: (f) => (f === 0 ? "brick_grey" : "panel_lt"),
    doors: [{ id: "main_entrance", side: "front", c: -11.6, w: 2.4, h: 2.5, double: true, glass: true, room: "g_lobby" },
      { id: "ambulance_doors", side: "rear", c: 10, w: 2.0, h: 2.4, double: true, room: "g_ambulance" },
      { id: "east_exit", side: "east", c: 0, w: 1.0, room: "g_corridor" }],
    glaze: (f) => (f === 0 ? { front: [[-17.8, -13.0, 0.1, 3.2], [-10.2, -5.4, 0.1, 3.2], [-4.6, 20.4, 0.9, 3.1, 3.6]], rear: [[-20.5, 8.8, 0.9, 3.1, 3.7], [11.2, 20.5, 0.9, 3.1, 3.1]], west: [[-1.0, 1.0, 0.9, 3.1]] }
      : { front: [[-20.5, 20.5, 0.9, 3.1, 4.5]], rear: [[-20.5, 20.5, 0.9, 3.1, 4.5]], west: [[-1.0, 1.0, 0.9, 3.1]], east: [[-1.0, 1.0, 0.9, 3.1]] }),
    plan: (f, { w }) => (f === 0 ? dl(w, { pre: "g_", zc,
      front: [{ x1: -20.93, skip: 1 }, { x1: -18.2, id: "wc", type: "bathroom" }, { x1: -5, id: "lobby", type: "lobby", open: 1 }, { x1: 1.5, id: "admissions", type: "office" }, { x1: 8.5, id: "consult_1", type: "office" }, { x1: 15, id: "consult_2", type: "office" }, { x1: 20.93, id: "office", type: "office" }, { skip: 1 }],
      rear: [{ x1: -20.9, skip: 1 }, { x1: -13, id: "ward_1", type: "ward" }, { x1: -9, id: "nurse", type: "office", open: 1 }, { x1: -1, id: "ward_2", type: "ward" }, { x1: 7, id: "ward_3", type: "ward" }, { x1: 13, id: "ambulance", type: "utility", dw: 1.8, dh: 2.4 }, { x1: 20.9, id: "staff", type: "office" }, { skip: 1 }] })
      : dl(w, { pre: `f${f}_`, zc,
        front: [{ x1: -20.93, skip: 1 }, { x1: -18.2, id: "wc", type: "bathroom" }, { x1: -11, id: "ward_a", type: "ward" }, { x1: -4, id: "ward_b", type: "ward" }, { x1: 0, id: "nurse", type: "office", open: 1 }, { x1: 7, id: "ward_c", type: "ward" }, { x1: 14, id: "ward_d", type: "ward" }, { x1: 20.93, id: "office", type: "office" }, { skip: 1 }],
        rear: [{ x1: -20.9, skip: 1 }, { x1: -13, id: "ward_e", type: "ward" }, { x1: -5, id: "ward_f", type: "ward" }, { x1: 3, id: "ward_g", type: "ward" }, { x1: 11, id: "ward_h", type: "ward" }, { x1: 16, id: "utility", type: "utility" }, { x1: 20.9, id: "office_r", type: "office" }, { skip: 1 }] })),
    roof: (A, w) => {
      const y = w.top;
      hvac(A, 4, -5.5, y, 3.0, 1.6); hvac(A, 9, -5.5, y, 3.0, 1.6); hvac(A, 14, -5.5, y, 3.0, 1.6);
      plantScreen(A, 2.0, 16.4, -7.4, -3.4, y, 1.8);
      tank(A, 9, 4.5, y); A.cyl([15, y + 1.6, 4.5], [0.45, 3.2, 0.45], "lift_steel", { seg: 12 }); A.cyl([16.2, y + 1.6, 4.5], [0.45, 3.2, 0.45], "lift_steel", { seg: 12 });   // flues
      // helipad: a raised deck on the west half, steps up from the roof, markings, edge lights, safety nets, windsock
      const hx0 = -20.5, hx1 = -5.5, hz0 = -7.5, hz1 = 7.5, py = y + 0.6, cx = (hx0 + hx1) / 2;
      A.box([hx0, y, hz0], [hx1, py, hz1], "concrete_wash", { bv: 0.03 });
      for (let i = 0; i < 3; i++) A.box([hx1, y + i * 0.15, -1.2], [hx1 + (3 - i) * 0.3, y + (i + 1) * 0.15, 1.2], "precast", { bv: 0.01 });
      for (const [a, b, c, d] of [[hx0 + 0.4, hx1 - 0.4, hz0 + 0.4, hz0 + 0.7], [hx0 + 0.4, hx1 - 0.4, hz1 - 0.7, hz1 - 0.4], [hx0 + 0.4, hx0 + 0.7, hz0 + 0.7, hz1 - 0.7], [hx1 - 0.7, hx1 - 0.4, hz0 + 0.7, hz1 - 0.7]])
        A.box([a, py, c], [b, py + 0.008, d], "paint_w", L1);
      A.cyl([cx, py + 0.004, 0], [11, 0.006, 11], "paint_w", { k: 0, seg: 32 });
      A.cyl([cx, py + 0.008, 0], [10.4, 0.006, 10.4], "panel_dk", { k: 0, seg: 32 });
      A.box([cx - 1.6, py + 0.011, -2.2], [cx - 1.0, py + 0.015, 2.2], "paint_w", K0); A.box([cx + 1.0, py + 0.011, -2.2], [cx + 1.6, py + 0.015, 2.2], "paint_w", K0);
      A.box([cx - 1.0, py + 0.011, -0.3], [cx + 1.0, py + 0.015, 0.3], "paint_w", K0);
      for (const [lx, lz] of [[hx0 + 0.2, hz0 + 0.2], [hx1 - 0.2, hz0 + 0.2], [hx1 - 0.2, hz1 - 0.2], [hx0 + 0.2, hz1 - 0.2], [cx, hz0 + 0.2], [cx, hz1 - 0.2], [hx0 + 0.2, 0]]) A.box([lx - 0.12, py, lz - 0.12], [lx + 0.12, py + 0.1, lz + 0.12], "lamp_g", L1);
      for (const [a, b, c, d, rx, rz] of [[hx0, hx1, hz0 - 0.75, hz0, -15, 0], [hx0, hx1, hz1, hz1 + 0.75, 15, 0], [hx0 - 0.75, hx0, hz0, hz1, 0, 15]]) {
        A.obox([(a + b) / 2, py - 0.1, (c + d) / 2], [b - a, 0.03, d - c], [rx, 0, rz], "louvre", L1);   // safety net trays
      }
      A.cyl([hx0 + 0.6, py + 1.5, hz1 - 0.6], [0.08, 3.0, 0.08], "paint_w", { k: 0, seg: 8 });
      A.cone([hx0 + 1.2, py + 2.85, hz1 - 0.6], [0.4, 1.3, 0.4], "orange", { k: 0, top: 0.4, r: [0, 0, -90], seg: 12 });
    },
    dress: (A, w) => {
      for (let f = 0; f < 4; f++) {   // nurse station counters
        const y = GF + f * h, [x0, x1, s, zIn] = f === 0 ? [-12.9, -9.1, -1, -1.3] : [-3.9, -0.1, 1, 1.3];
        A.box([x0 + 0.3, y, Math.min(zIn + s * 0.9, zIn + s * 1.5)], [x1 - 0.3, y + 1.05, Math.max(zIn + s * 0.9, zIn + s * 1.5)], "laminate", { bv: 0.01 });
        A.box([x0 + 0.25, y + 1.05, Math.min(zIn + s * 0.85, zIn + s * 1.55)], [x1 - 0.25, y + 1.09, Math.max(zIn + s * 0.85, zIn + s * 1.55)], "panel_acc", K0);
        A.box([x1 - 0.9, y + 1.09, Math.min(zIn + s * 1.1, zIn + s * 1.15)], [x1 - 0.4, y + 1.4, Math.max(zIn + s * 1.1, zIn + s * 1.15)], "screen", L1);
      }
      A.box([-15.5, GF, 5.2], [-11.5, GF + 1.05, 5.9], "laminate", { bv: 0.01 });   // reception desk
      A.box([-15.55, GF + 1.05, 5.15], [-11.45, GF + 1.1, 5.95], "panel_acc", K0);
      canopy(A, { x0: -16.5, x1: -6.5, z0: 8.5, z1: 14, y: 3.9, cols: [[-15.8, 13.4], [-13.4, 13.4], [-9.8, 13.4], [-7.2, 13.4]] });
      A.box([-16.5, -0.2, 8.5], [-6.5, 0.24, 14], "paving", { bv: 0.02 });   // drop-off apron
      A.box([-18, -0.2, 14.0], [-5, 0.15, 14.3], "precast", { bv: 0.02 });    // kerb
      fbox(A, sideF(w, "front"), -15.5, -7.5, 4.45, 5.0, 0, 0.05, "sign_blank", L1);
      fbox(A, sideF(w, "front"), 22.0, 23.5, w.top - 2.2, w.top - 0.4, 0, 0.06, "paint_w", L1);   // blank wayfinding panel
      for (const s of ["east", "west"]) for (let f = 1; f < 4; f++) for (const a of [-6.5, -4.5, 4.5, 6.5]) fbox(A, sideF(w, s), a - 0.06, a + 0.06, GF + f * h, GF + (f + 1) * h - 0.3, 0, 0.3, "alu_lt", L1);   // end-wall fins
    },
  });
  const cor = ["g_corridor", "f1_corridor", "f2_corridor", "f3_corridor"];
  buildCore(A, kW, w.top - SLAB, cor); buildCore(A, kE, 0, cor);
  buildLift(A, lw, w.top); buildLift(A, le, w.top);
  addRoom(A, "roof", "utility", -23.7, roofY, -8.2, 23.7, roofY + 2.5, 8.2);
  navDoor(A, "f3_corridor", "roof", [kE.xa + kE.len / 2, roofY, kE.zF]);
  coverRing(A, -24, 24, -8.5, 8.5);
  doorTour(A, "main_entrance", -11.6, 8.5, 8.2, GF, "mod_hospital_a_lobby");
  return finish(A);
}

// ============================================================================ 3. leisure centre
export function leisureCentre() {
  const A = mk("mod_bld_leisure", "Leisure centre", "Sports and leisure centre, 52 x 34 m: a glazed single-storey front range (lobby with entrance canopy, cafe, office, weights room, first aid, changing rooms) before a tall 28 x 22 m sports hall in ribbed cladding with clerestories and a 24 x 22 m pool hall. The pool is a real sunken 16 x 11 m tank, tiled, 1.4 m deep with water, lane markings and ropes, steps in the shallow end, ladders, starting blocks and spectator seating; the sports hall has a court, backboards and bleachers; changing rooms have locker banks.",
    1, [52, 37], [0.04, 0.30, 0.46]);
  const zc = [2.0, 4.2];
  const pool = [6, -19.5, 22, -8.5];   // the water: x0, z0, x1, z1
  wing(A, {
    tag: "f", x0: -26, x1: 26, z0: -2, z1: 10, n: 1, h: 4.2, omit: ["rear"], par: 0.9, band: null, skin: () => "render_w",
    special: [[-25.7, 4.32, -12.06, 9.7, "concrete_floor"], [2.06, -2, 17.94, 1.88, "terracotta"]],
    doors: [{ id: "main_entrance", side: "front", c: -3, w: 2.4, h: 2.5, double: true, glass: true, room: "f_lobby" },
      { id: "weights_exit", side: "west", c: 7, w: 1.0, room: "f_weights" }, { id: "plant_door", side: "east", c: -0.2, w: 1.2, room: "f_plant" }],
    glaze: () => ({ front: [[-25.2, -12.6, 0.9, 3.4, 4.2], [-11.5, -4.5, 0.1, 3.6, 3.5], [-1.5, 5.6, 0.1, 3.6, 3.55], [6.4, 13.6, 0.9, 3.4, 3.6], [14.4, 25.2, 0.9, 3.4, 3.6]], east: [[4.8, 9.2, 0.9, 3.4]] }),
    plan: (f, { w }) => dl(w, { pre: "f_", zc,
      front: [{ x1: -12, id: "weights", type: "gym" }, { x1: 6, id: "lobby", type: "lobby", open: 1 }, { x1: 14, id: "cafe", type: "dining", dw: 2.0 }, { id: "office", type: "office" }],
      rear: [{ x1: -18, id: "store", type: "utility" }, { x1: -14, id: "passage", type: "hall", open: 1 }, { x1: -8, id: "first_aid", type: "office" }, { x1: 2, id: "admin", type: "office" }, { x1: 10, id: "change_m", type: "utility" }, { x1: 18, id: "change_f", type: "utility" }, { id: "plant", type: "utility" }] }),
    roof: (A, w) => { hvac(A, 20, 7, w.top); hvac(A, 20, 3, w.top); },
    dress: (A, w) => {
      for (const [x0, x1] of [[2.4, 9.6], [10.4, 17.6]]) lockers(A, x0, x1, 1.88, -1, GF);   // locker banks on the corridor walls of the changing rooms
      A.box([-25.6, GF + 0.3, 4.4], [-25.65, GF + 2.2, 9.6], "glass_int", L1);              // weights-room mirror
      A.box([-6.5, GF, 7.6], [-2.0, GF + 1.05, 8.4], "laminate", { bv: 0.01 });             // reception desk
      A.box([-6.55, GF + 1.05, 7.55], [-1.95, GF + 1.1, 8.45], "panel_acc", K0);
      canopy(A, { x0: -7.5, x1: 1.5, z0: 10, z1: 13, y: 3.4, cols: [[-7.0, 12.6], [1.0, 12.6]] });
      fbox(A, sideF(w, "front"), -7, 1, 3.8, 4.4, 0, 0.05, "sign_blank", L1);
    },
  });
  wing(A, {
    tag: "s", x0: -26, x1: 2, z0: -24, z1: -2, n: 1, h: 8.0, fm: "plank_x", band: null, skin: () => "corrugated",
    links: [{ side: "front", c: -16, w: 2.4, h: 2.5, a: "f_passage", b: "sports_hall" }],
    doors: [{ id: "hall_exit", side: "rear", c: -12, w: 1.8, double: true, room: "sports_hall" }],
    glaze: () => ({ front: [[-25.2, 1.2, 5.4, 7.6, 4.4]], rear: [[-25.2, 1.2, 5.4, 7.6, 4.4]], west: [[-23.2, -2.8, 5.4, 7.6, 4.1]] }),
    plan: () => ({ rooms: [{ id: "sports_hall", type: "gym", x0: -25.7, z0: -23.7, x1: 1.7, z1: -2.3 }], walls: [] }),
    roof: (A, w) => { for (const x of [-20, -12, -4]) { A.cyl([x, w.top + 0.6, -13], [1.0, 1.2, 1.0], "louvre", { seg: 12 }); A.cone([x, w.top + 1.35, -13], [1.3, 0.3, 1.3], "alu", { k: 0, top: 0.1, seg: 12 }); } },
    dress: (A, w) => {
      backboard(A, -25.7, 1, -13, GF); backboard(A, 1.7, -1, -13, GF);
      courtLines(A, -24.5, 0.5, -21.5, -4.5, GF);
      bleachers(A, -21, -4, -23.7, -1, GF, 4);
      trusses(A, -25.7, 1.7, -23.7, -2.3, w.top - SLAB - 0.02, 4.5);
      for (const [x0, x1] of [[-25.66, -25.62], [1.62, 1.66]]) A.box([x0, GF, -20], [x1, GF + 2.0, -6], "panel_acc", L1);
      for (let z = -22; z < -2; z += 4.4) A.box([-26.075, GF - 0.05, z - 0.05], [-26.0, w.top + 1.1, z + 0.05], "panel_dk", L1);
    },
  });
  wing(A, {
    tag: "p", x0: 2, x1: 26, z0: -24, z1: -2, n: 1, h: 6.0, omit: ["west"], fm: "terracotta", band: null, skin: () => "render_w",
    groundHoles: [[5.7, -19.8, 22.3, -8.2]],
    links: [{ side: "front", c: 6, w: 1.2, h: 2.2, a: "f_change_m", b: "pool_hall" }, { side: "front", c: 14, w: 1.2, h: 2.2, a: "f_change_f", b: "pool_hall" }],
    doors: [{ id: "pool_exit", side: "east", c: -21.5, w: 1.0, room: "pool_hall" }],
    glaze: () => ({ rear: [[3.0, 25.2, 0.9, 5.2, 3.7]], east: [[-19.5, -2.8, 0.9, 5.2, 4.2]] }),
    plan: () => ({ rooms: [{ id: "pool_hall", type: "lobby", x0: 2, z0: -23.7, x1: 25.7, z1: -2.3 }], walls: [] }),
    roof: (A, w) => { for (const x of [8, 14, 20]) { A.box([x - 1.2, w.top, -17.5], [x + 1.2, w.top + 0.5, -10.5], "alu", { bv: 0.02 }); A.box([x - 1.1, w.top + 0.5, -17.4], [x + 1.1, w.top + 0.52, -10.6], "glass_t", K0); } },
    dress: (A, w) => {
      const [px0, pz0, px1, pz1] = pool, bot = -1.25, lvl = 0.12;
      A.box([px0 - 0.3, bot - 0.3, pz0 - 0.3], [px1 + 0.3, bot, pz1 + 0.3], "pool_tile");
      A.box([px0 - 0.3, bot, pz0 - 0.3], [px0, GF - 0.05, pz1 + 0.3], "pool_tile"); A.box([px1, bot, pz0 - 0.3], [px1 + 0.3, GF - 0.05, pz1 + 0.3], "pool_tile");
      A.box([px0, bot, pz0 - 0.3], [px1, GF - 0.05, pz0], "pool_tile"); A.box([px0, bot, pz1], [px1, GF - 0.05, pz1 + 0.3], "pool_tile");
      for (const [a, b, c, d] of [[px0 - 0.3, px1 + 0.3, pz0 - 0.3, pz0], [px0 - 0.3, px1 + 0.3, pz1, pz1 + 0.3], [px0 - 0.3, px0, pz0, pz1], [px1, px1 + 0.3, pz0, pz1]])
        A.box([a, GF - 0.05, c], [b, GF + 0.01, d], "precast", { bv: 0.015 });                              // coping
      for (const [a, b, c, d] of [[px0, px1, pz0 + 0.02, pz0 + 0.14], [px0, px1, pz1 - 0.14, pz1 - 0.02]]) A.box([a, lvl - 0.04, c], [b, lvl - 0.03, d], "panel_dk", L1);   // scum channel grilles
      for (let i = 0; i < 4; i++) {                                                                       // lane markings and T-ends
        const z = pz0 + 1.375 + i * 2.75;
        A.box([px0 + 1.5, bot, z - 0.12], [px1 - 1.5, bot + 0.007, z + 0.12], "panel_dk", L1);
        for (const x of [px0 + 1.5, px1 - 1.5]) A.box([x - 0.12, bot, z - 0.5], [x + 0.12, bot + 0.014, z + 0.5], "panel_dk", L1);
        A.box([px1 + 0.35, GF, z - 0.25], [px1 + 0.85, GF + 0.7, z + 0.25], "precast", { bv: 0.02 });       // starting block
        A.obox([px1 + 0.55, GF + 0.72, z], [0.5, 0.05, 0.5], [0, 0, 10], "rubber", L1);
      }
      for (let i = 1; i < 4; i++) A.cyl([(px0 + px1) / 2, lvl, pz0 + i * 2.75], [0.08, px1 - px0 - 0.2, 0.08], i === 2 ? "orange" : "panel_acc", { k: 0, lod: 1, r: [0, 0, 90], seg: 8 });   // lane ropes
      for (let i = 0; i < 4; i++) A.box([px0, bot + i * 0.35, -12.0], [px0 + (4 - i) * 0.35, bot + (i + 1) * 0.35, -9.0], "pool_tile");   // shallow-end steps
      for (const z of [pz0 + 0.6, pz1 - 0.6]) for (const dz of [-0.25, 0.25]) {                         // ladders at the deep end
        A.cyl([px1 - 0.18, (bot + 0.35 + GF + 0.75) / 2, z + dz], [0.045, GF + 0.75 - bot - 0.35, 0.045], "lift_steel", { k: 0, lod: 1, seg: 8 });
        A.box([px1 - 0.2, GF + 0.72, z + dz - 0.022], [px1 + 0.55, GF + 0.77, z + dz + 0.022], "lift_steel", L1);
      }
      for (const z of [pz0 + 0.6, pz1 - 0.6]) for (let k = 0; k < 3; k++) A.box([px1 - 0.25, bot + 0.6 + k * 0.3, z - 0.25], [px1 - 0.12, bot + 0.64 + k * 0.3, z + 0.25], "lift_steel", L1);
      A.box([px0, lvl - 0.01, pz0], [px1, lvl, pz1], "water_proxy", K0);
      (A.meta.water ??= { surfaces: [] }).surfaces.push({ outline: [[px0, pz0], [px1, pz0], [px1, pz1], [px0, pz1]], level: lvl, kind: "pond", clarity: 4, waves: 0.12, tint: [0.05, 0.32, 0.40] });
      A._nav.areas.push({ outline: [[px0, pz0], [px1, pz0], [px1, pz1], [px0, pz1]], area: "water" });
      bleachers(A, 16.5, 25.4, -2.3, 1, GF, 3, 0.75, 0.42);
      A.box([3.0, GF, -21.2], [3.1, GF + 2.0, -20.8], "panel_acc", L1); A.box([2.7, GF + 1.6, -21.3], [3.4, GF + 1.66, -20.7], "panel_acc", L1);   // lifeguard chair
      A.box([2.75, GF + 1.66, -20.8], [3.35, GF + 2.2, -20.74], "panel_acc", L1);
      trusses(A, 2, 25.7, -23.7, -2.3, w.top - SLAB - 0.02, 4.4, false);
    },
  });
  coverRing(A, -26, 26, -24, 10);
  doorTour(A, "main_entrance", -3, 10, 9.7, GF, "mod_leisure_a_lobby");
  return finish(A, [{ outline: [[5.4, -20.1], [22.6, -20.1], [22.6, -7.9], [5.4, -7.9]], depth: 1.9, bank: 0.3 }]);
}

// ============================================================================ 4. big-box store
export function bigBox() {
  const A = mk("mod_bld_bigbox", "Big-box store", "Generic big-box retailer, 58 x 40 m and 7.5 m tall: ribbed metal cladding on a concrete base, a tall entrance portal with a blank sign band and canopy over a glazed vestibule, skylights and rooftop units. Inside: the vestibule, a customer-service office and a cafe along the front, a 58 x 26 m sales floor with steel columns, shelving gondolas and six checkout lanes, a stockroom behind with three roller doors onto a sunken loading dock (truck well, bumpers, dock shelters, canopy). Brings a cart corral for the car park.",
    1, [58, 40], [0.55, 0.12, 0.08]);
  const x0 = -29, x1 = 29, z0 = -20, z1 = 20;
  wing(A, {
    tag: "m", x0, x1, z0, z1, n: 1, h: 7.2, fm: "concrete_floor", band: null, par: 1.2, skin: () => "corrugated",
    special: [[-28.7, 15.06, 28.7, 19.7, "floor_x"]],
    doors: [{ id: "entrance_l", side: "front", c: -2.2, w: 2.2, h: 2.5, double: true, glass: true, room: "vestibule" },
      { id: "entrance_r", side: "front", c: 2.2, w: 2.2, h: 2.5, double: true, glass: true, room: "vestibule" },
      { id: "staff_door", side: "front", c: -25, w: 1.0, room: "office" },
      { id: "fire_exit_w", side: "west", c: 0, w: 1.8, double: true, room: "sales" }, { id: "fire_exit_e", side: "east", c: 0, w: 1.8, double: true, room: "sales" },
      ...[-16, -9, -2].map((c, i) => ({ id: `dock_door_${i + 1}`, side: "rear", c, w: 3.6, h: 4.2, roller: true, room: "stock", apron: false })),
      { id: "dock_personnel", side: "rear", c: 4.0, w: 1.0, room: "stock", apron: false }],
    glaze: () => ({ front: [[-4.8, -3.5, 0.1, 2.5], [-0.9, 0.9, 0.1, 2.5], [3.5, 4.8, 0.1, 2.5], [-4.8, 4.8, 2.75, 4.6, 3.2], [6.0, 27.0, 0.9, 3.4, 3.5], [-22.5, -8.0, 0.9, 3.4, 3.6]] }),
    plan: () => ({
      rooms: [{ id: "vestibule", type: "lobby", x0: -4.94, z0: 15.06, x1: 4.94, z1: 19.7 }, { id: "office", type: "office", x0: -28.7, z0: 15.06, x1: -5.06, z1: 19.7 },
        { id: "cafe", type: "dining", x0: 5.06, z0: 15.06, x1: 28.7, z1: 19.7 }, { id: "sales", type: "shop", x0: -28.7, z0: -10.94, x1: 28.7, z1: 14.94 },
        { id: "stock", type: "utility", x0: -28.7, z0: -19.7, x1: 28.7, z1: -11.06 }],
      walls: [{ axis: "x", d: 15.0, a0: -28.7, a1: 28.7, holes: [{ c: -20, w: 1.0, a: "office", b: "sales" }, { c: -2.2, w: 2.4, h: 2.6, a: "vestibule", b: "sales" }, { c: 2.2, w: 2.4, h: 2.6, a: "vestibule", b: "sales" }, { c: 16, w: 4.0, h: 3.0, a: "cafe", b: "sales" }] },
        { axis: "z", d: -5.0, a0: 15.06, a1: 19.7, holes: [] }, { axis: "z", d: 5.0, a0: 15.06, a1: 19.7, holes: [] },
        { axis: "x", d: -11.0, a0: -28.7, a1: 28.7, holes: [{ c: -18, w: 3.2, h: 3.2, a: "sales", b: "stock" }, { c: 8, w: 3.2, h: 3.2, a: "sales", b: "stock" }, { c: 20, w: 1.0, a: "sales", b: "stock" }] }],
    }),
    roof: (A, w) => {
      for (const x of [-18, -6, 6, 18]) for (const z of [-4, 8]) { A.box([x - 1.4, w.top, z - 1.4], [x + 1.4, w.top + 0.45, z + 1.4], "alu", { bv: 0.02 }); A.prism(0, x - 1.3, x + 1.3, [[z - 1.3, w.top + 0.45], [z + 1.3, w.top + 0.45], [z, w.top + 0.9]], "glass_t", K0); }
      for (const [x, z] of [[-24, -15], [-12, -15], [0, -15], [12, -15], [24, -15]]) hvac(A, x, z, w.top, 3.2, 1.8, 1.5);
      A.box([-26, w.top, 1.0], [26, w.top + 0.5, 1.4], "alu_lt", L1);   // duct run
    },
    dress: (A, w) => {
      const y = GF, F = sideF(w, "front");
      // entrance portal, sign band, canopy
      for (const x of [-6.6, 6.0]) fbox(A, F, x, x + 0.6, -0.1, w.top + 0.4, 0, 0.6, "panel_acc", { bv: 0.02 });
      fbox(A, F, -6.6, 6.6, w.top + 0.4, w.top + 3.2, 0, 0.6, "panel_acc", { bv: 0.02 });
      fbox(A, F, -5.4, 5.4, w.top + 0.9, w.top + 2.7, 0.6, 0.65, "sign_blank", L1);
      canopy(A, { x0: -6.0, x1: 6.0, z0: 20, z1: 23.5, y: 4.9, cols: [[-5.6, 23.1], [5.6, 23.1]], fascia: "panel_acc" });
      A.box([-6.0, -0.2, 20], [6.0, 0.24, 23.5], "paving", { bv: 0.02 });
      for (let x = -27; x < 28; x += 6) if (Math.abs(x) > 7) fbox(A, F, x - 0.12, x + 0.12, 0.1, w.top + 1.2, 0, 0.12, "panel_dk", L1);   // cladding ribs
      for (const s of ["east", "west"]) for (let z = -17; z < 18; z += 6) fbox(A, sideF(w, s), z - 0.12, z + 0.12, 0.1, w.top + 1.2, 0, 0.12, "panel_dk", L1);
      for (const s of SIDES) { const G = sideF(w, s), isX = G.axis === "x"; fbox(A, G, isX ? x0 - 0.02 : z0 + 0.005, isX ? x1 + 0.02 : z1 - 0.005, -0.3, 1.2, -0.012, 0.02, "concrete", { k: 0, bv: 0.01 }); }   // concrete base course
      // structure, lights
      for (const x of [-14.4, 0, 14.4]) { A.box([x - 0.18, y, -3.18], [x + 0.18, w.top - SLAB, -2.82], "panel_dk"); A.box([x - 0.12, w.top - SLAB - 0.6, -19.7], [x + 0.12, w.top - SLAB - 0.02, 19.7], "panel_dk", L1); }
      for (const x of [-21, -7, 7, 21]) A.box([x - 0.1, w.top - SLAB - 1.0, -10], [x + 0.1, w.top - SLAB - 0.95, 14], "light_panel", L1);
      // shelving gondolas (fixed): plinth, spine, three shelves either side, a header
      for (const x of [-24, -19.5, -15, -10.5, 10.5, 15, 19.5, 24]) {
        A.box([x - 0.55, y, -8], [x + 0.55, y + 0.12, 6], "panel_dk");
        A.box([x - 0.04, y + 0.12, -8], [x + 0.04, y + 2.1, 6], "panel_lt");
        for (const sy of [0.55, 1.05, 1.55]) A.box([x - 0.5, y + sy, -7.95], [x + 0.5, y + sy + 0.03, 5.95], "panel_lt", L1);
        A.box([x - 0.06, y + 2.1, -8], [x + 0.06, y + 2.4, 6], "panel_acc", L1);
        A.box([x - 0.55, y, 6], [x + 0.55, y + 1.6, 6.5], "panel_acc", { bv: 0.01 });   // end cap
      }
      // checkout lanes
      for (const x of [-15, -12, -9, 9, 12, 15]) {
        A.box([x - 0.45, y, 10.2], [x + 0.45, y + 0.9, 12.8], "panel_lt", { bv: 0.01 });
        A.box([x - 0.35, y + 0.9, 10.3], [x + 0.35, y + 0.92, 12.0], "rubber", K0);          // belt
        A.box([x - 0.4, y + 0.9, 12.1], [x + 0.4, y + 1.15, 12.7], "panel_dk", L1);          // till
        A.box([x + 0.15, y + 1.15, 12.3], [x + 0.35, y + 1.45, 12.32], "screen", L1);
        A.cyl([x + 0.38, y + 1.3, 10.3], [0.05, 2.6, 0.05], "alu_lt", { k: 0, lod: 1, seg: 8 });   // lane light
        A.box([x + 0.25, y + 2.6, 10.2], [x + 0.51, y + 2.85, 10.4], "sign_blank", L1);
      }
      // loading dock: truck-well face, bumpers, canopy, bollards
      A.box([-23, -1.15, -20.35], [5.5, GF - 0.05, -20.0], "concrete");
      for (const c of [-16, -9, -2]) for (const e of [-1.2, 1.2]) A.box([c + e - 0.15, GF - 0.45, -20.6], [c + e + 0.15, GF - 0.05, -20.35], "rubber", { bv: 0.02 });
      A.box([-19, 5.3, -23.6], [2, 5.5, -20], "panel_lt", { bv: 0.02 });
      for (const x of [-18.6, -12.5, -5.5, 1.6]) A.obox([x, 5.85, -21.8], [0.04, 0.04, 4.4], [-20, 0, 0], "alu", L1);   // canopy hangers
      for (const x of [3.2, 4.8]) A.cyl([x, 0.5, -21.0], [0.2, 1.0, 0.2], "safety_yel", { seg: 10 });
      A.box([2.6, 0, -21.6], [5.4, GF - 0.03, -20.35], "precast", { bv: 0.02 });   // landing at the personnel door
      for (let i = 0; i < 3; i++) A.box([2.6, 0.08 * i, -21.6 - (3 - i) * 0.3], [3.4, 0.08 * (i + 1), -21.6], "precast", K0);
    },
  });
  A.meta.companions = [comp("mod_cart_corral", 13, 25, 0), comp("mod_cart_corral", -13, 27, 0)];
  coverRing(A, x0, x1, z0, z1);
  doorTour(A, "entrance_l", -2.2, 20, 19.7, GF, "mod_bigbox_a_vestibule");
  return finish(A, [{ outline: [[-23, -36], [5.5, -36], [5.5, -14], [-23, -14]], depth: 1.15, bank: 4.0 }]);
}

// cart corral (companion of the big-box store): tube rails, a roof, blank sign, a row of nested carts
export function cartCorral() {
  const A = new Asset({ id: "mod_cart_corral", version: 1, name: "Cart corral", category: "Modern", description: "Car-park cart corral, 2.4 x 6 m: galvanised tube rails on posts, a light canopy with a blank sign panel and five nested shopping carts.",
    footprint: [2.6, 6.2], floors: 0, enterable: false, plinthDepth: 0, maxGrade: 0.6, noAutoPanels: true });
  modMats(A, [0.55, 0.12, 0.08]);
  for (const x of [-1.15, 1.15]) {
    for (const z of [-2.9, -1, 1, 2.9]) A.cyl([x, 1.25, z], [0.07, 2.5, 0.07], "alu_lt", { k: 0, seg: 8 });
    for (const y of [0.45, 0.95]) A.cyl([x, y, 0], [0.05, 5.8, 0.05], "alu_lt", { k: 0, r: [90, 0, 0], seg: 8 });
    A.box([x - 0.05, 0, -2.95], [x + 0.05, 1.0, 2.95], "alu_lt", { v: 0 });
  }
  A.cyl([0, 0.95, -2.9], [0.05, 2.3, 0.05], "alu_lt", { k: 0, r: [0, 0, 90], seg: 8 });
  A.box([-1.15, 0.9, -2.95], [1.15, 1.0, -2.85], "alu_lt", { v: 0 });
  A.box([-1.3, 2.5, -3.1], [1.3, 2.58, 3.1], "panel_lt", { bv: 0.01 });
  A.box([-0.8, 2.58, 2.9], [0.8, 3.1, 2.96], "sign_blank", K0);
  for (let i = 0; i < 5; i++) {
    const z = -2.1 + i * 0.32;
    A.box([-0.3, 0.45, z - 0.45], [0.3, 0.95, z + 0.45], "lift_steel", K0);         // wire basket (solid stand-in)
    A.box([-0.32, 0.93, z - 0.47], [0.32, 0.97, z + 0.47], "alu_lt", L1);
    A.box([-0.28, 0.98, z + 0.45], [0.28, 1.03, z + 0.5], "orange", L1);            // handle
    A.box([-0.25, 0.1, z - 0.4], [0.25, 0.14, z + 0.4], "alu_lt", L1);              // chassis
    for (const [wx, wz] of [[-0.22, -0.35], [0.22, -0.35], [-0.22, 0.35], [0.22, 0.35]]) A.cyl([wx, 0.06, z + wz], [0.03, 0.12, 0.12], "rubber", { k: 0, lod: 1, r: [0, 0, 90], seg: 8 });
  }
  A.box([-0.33, 0, -2.6], [0.33, 1.05, -0.4], "lift_steel", { v: 0 });
  setNav(A, { effect: "carve", rooms: [], doors: [], entrances: [], corners: [], firingPoints: [], cover: [cover([1.6, 0, 0], 270, "low", ["over"]), cover([-1.6, 0, 0], 90, "low", ["over"])], areas: [] });
  return A;
}

// ============================================================================ 5. train station
export function trainStation() {
  const A = mk("mod_bld_station", "Train station", "Two-platform through station: a glazed 24 x 14 m concourse hall raised to platform level behind front steps (ticket office with a counter window, ticket barriers, ticket machines, a departures board), two 56 m platforms with yellow-lined copings and steel canopies, two tracks on ballast with concrete sleepers and rails, and an enclosed footbridge 6 m above the tracks with a two-flight glass-guarded stair down to each platform.",
    1, [56, 38], [0.05, 0.20, 0.45]);
  const gf = 0.9, X = 28;
  const w = wing(A, {
    tag: "c", x0: -12, x1: 12, z0: 4, z1: 18, n: 1, h: 6.0, gf, band: null, skin: () => "stone_clad", finMat: "alu_lt",
    doors: [{ id: "entrance_w", side: "front", c: -6, w: 2.0, h: 2.5, double: true, glass: true, room: "concourse", apron: false },
      { id: "entrance_e", side: "front", c: 0, w: 2.0, h: 2.5, double: true, glass: true, room: "concourse", apron: false },
      { id: "platform_door_w", side: "rear", c: -6, w: 2.0, h: 2.5, double: true, glass: true, room: "concourse", apron: false },
      { id: "platform_door_e", side: "rear", c: 0, w: 2.0, h: 2.5, double: true, glass: true, room: "concourse", apron: false },
      { id: "staff_door", side: "rear", c: 9, w: 1.0, room: "ticket_office", apron: false }],
    glaze: () => ({ front: [[-11.2, -7.4, 0.2, 4.0], [-4.6, -1.4, 0.2, 4.0], [1.4, 5.5, 0.2, 4.0], [6.5, 11.2, 0.9, 3.0], [-11.2, 11.2, 4.3, 5.6, 3.8]],
      rear: [[-11.2, 11.2, 4.3, 5.6, 3.8]], west: [[5.0, 17.0, 0.2, 4.0, 4.0]], east: [[12.0, 17.0, 0.9, 3.0]] }),
    plan: () => ({ rooms: [{ id: "concourse", type: "lobby", x0: -11.7, z0: 4.3, x1: 5.94, z1: 17.7 }, { id: "ticket_office", type: "office", x0: 6.06, z0: 4.3, x1: 11.7, z1: 17.7 }],
      walls: [{ axis: "z", d: 6.0, a0: 4.3, a1: 17.7, holes: [{ c: 6.0, w: 1.0, a: "concourse", b: "ticket_office" }, { c: 12.0, w: 3.0, y0: 0.95, h: 2.0 }] }] }),
    roof: (A, w) => { A.box([-9, w.top, 9.5], [9, w.top + 0.6, 12.5], "alu", { bv: 0.02 }); A.prism(0, -8.9, 8.9, [[9.6, w.top + 0.6], [12.4, w.top + 0.6], [11, w.top + 1.4]], "glass_t", K0); hvac(A, -8, 15.5, w.top); },
    dress: (A, w) => {
      // ticket counter and screen in the hatch, ticket barriers, machines, departures board
      A.box([6.06, gf, 10.3], [6.9, gf + 0.95, 13.7], "laminate", { bv: 0.01 });
      A.box([5.98, gf + 0.95, 10.4], [6.02, gf + 2.95, 13.6], "glass_int");
      for (let i = 0; i < 6; i++) { const x = -8.2 + i * 1.6; A.box([x - 0.15, gf, 5.0], [x + 0.15, gf + 1.05, 6.2], "lift_steel", { bv: 0.02 }); A.box([x - 0.12, gf + 1.05, 5.9], [x + 0.12, gf + 1.07, 6.15], "screen", L1); }
      for (let i = 0; i < 5; i++) { const x = -7.4 + i * 1.6; A.box([x - 0.6, gf + 0.4, 5.55], [x - 0.15, gf + 1.0, 5.57], "glass_int", L1); A.box([x + 0.15, gf + 0.4, 5.55], [x + 0.6, gf + 1.0, 5.57], "glass_int", L1); }
      for (const x of [-11.2, -10.2]) { A.box([x, gf, 14.0], [x + 0.75, gf + 1.7, 14.6], "panel_acc", { bv: 0.02 }); A.box([x + 0.15, gf + 1.1, 14.6], [x + 0.6, gf + 1.5, 14.62], "screen", L1); }
      A.box([-5.0, gf + 3.6, 4.32], [1.0, gf + 4.6, 4.42], "panel_dk", K0); A.box([-4.85, gf + 3.7, 4.42], [0.85, gf + 4.5, 4.44], "screen", L1);
      trusses(A, -11.7, 11.7, 4.3, 17.7, w.top - SLAB - 0.02, 4.0);
      // front terrace with steps down to the street
      A.box([-12, -0.3, 18], [12, gf - 0.02, 21], "paving", { bv: 0.02 });
      for (let i = 0; i < 5; i++) A.box([-9, -0.3, 21 + i * 0.32], [3, gf - 0.02 - (i + 1) * 0.176 + 0.02, 21 + (i + 1) * 0.32], "precast", { bv: 0.01 });
      canopy(A, { x0: -8, x1: 2, z0: 18, z1: 20.6, y: 3.5 + gf, cols: [] });
      fbox(A, sideF(w, "front"), -7.5, 1.5, gf + 4.55, gf + 4.95, 0.3, 0.33, "sign_blank", L1);
    },
  });
  // platforms, copings, tactile lines, canopies (gapped over the footbridge stairs)
  const platform = (pz0, pz1, edge, colZ, stairZ) => {
    const [cz0, cz1] = edge < 0 ? [pz0, pz0 + 0.4] : [pz1 - 0.4, pz1];
    A.box([-X, -0.5, edge < 0 ? pz0 + 0.4 : pz0], [X, gf, edge < 0 ? pz1 : pz1 - 0.4], "concrete_wash");
    A.box([-X, -0.5, cz0], [X, gf, cz1], "precast", { bv: 0.01 });
    const lz = edge < 0 ? pz0 + 0.55 : pz1 - 0.65;
    A.box([-X + 0.3, gf, lz], [X - 0.3, gf + 0.007, lz + 0.1], "safety_yel", L1);
    const cy = gf + 3.4, holes = [{ a0: 15.6, a1: 27.9, y0: stairZ[0] - 0.2, y1: stairZ[1] + 0.2 }];
    for (const [a, b, c, d] of cutRect(-X + 0.5, X - 0.5, pz0 + 0.1, pz1 - 0.1, holes)) { A.box([a, cy, c], [b, cy + 0.15, d], "panel_lt", { bv: 0.01 }); A.box([a, cy - 0.12, c], [b, cy, c + 0.12], "alu", L1); }
    for (let x = -24.5; x < X; x += 7) {
      if (x > 15 && x < 28 && colZ > stairZ[0] - 0.6 && colZ < stairZ[1] + 0.6) continue;
      A.cyl([x, gf + 1.7, colZ], [0.26, 3.4, 0.26], "panel_dk", { seg: 12 });
      A.box([x - 0.1, cy - 0.35, pz0 + 0.3], [x + 0.1, cy, pz1 - 0.3], "panel_dk", L1);
      A.box([x - 0.6, cy - 0.06, colZ - 0.15], [x + 0.6, cy - 0.04, colZ + 0.15], "light_panel", L1);
    }
  };
  platform(-1, 4, -1, 0.3, [1.4, 3.6]);
  platform(-14.8, -9.8, 1, -11.0, [-14.6, -12.4]);
  A.box([-X, gf, -14.8], [X, gf + 1.2, -14.65], "louvre");   // rear platform fence
  // tracks: ballast, sleepers, rails
  for (const zc of [-3.2, -7.6]) {
    A.box([-X, -0.2, zc - 1.9], [X, 0.18, zc + 1.9], "ballast", { k: 0 });
    A.box([-X, -0.2, zc - 1.9], [X, 0.0, zc + 1.9], "ballast", { v: 0 });
    for (let x = -X + 0.5; x < X; x += 1.1) A.box([x - 0.13, 0.12, zc - 1.25], [x + 0.13, 0.3, zc + 1.25], "concrete_wash", L1);
    for (const e of [-0.7525, 0.7525]) { A.box([-X, 0.3, zc + e - 0.07], [X, 0.33, zc + e + 0.07], "rail_steel", K0); A.box([-X, 0.33, zc + e - 0.018], [X, 0.47, zc + e + 0.018], "rail_steel", K0); A.box([-X + 0.01, 0.44, zc + e - 0.036], [X - 0.01, 0.48, zc + e + 0.036], "lift_steel", L1); }
  }
  // footbridge: deck, parapets (gapped where the stairs arrive), glazing, roof, columns; stairs to both platforms
  const bx0 = 14, bx1 = 16.4, bz0 = -14.6, bz1 = 3.6, dy = gf + 6.0;
  A.box([bx0, dy - 0.3, bz0], [bx1, dy, bz1], "concrete_wash", { bv: 0.02 });
  A.box([bx0 + 0.1, dy, bz0 + 0.1], [bx1 - 0.1, dy + 0.01, bz1 - 0.1], "rubber", K0);
  A.box([bx0 - 0.1, dy, bz0], [bx0, dy + 1.1, bz1], "panel_lt");
  for (const [a, b] of cutRect(bz0, bz1, 0, 1, [{ a0: 1.4, a1: 3.6, y0: -1, y1: 2 }, { a0: -14.6, a1: -12.4, y0: -1, y1: 2 }]).map((r) => [r[0], r[1]])) A.box([bx1, dy, a], [bx1 + 0.1, dy + 1.1, b], "panel_lt");
  for (const [x, s] of [[bx0 - 0.05, -1], [bx1 + 0.05, 1]]) {
    A.box([x - 0.03, dy + 1.1, bz0], [x + 0.03, dy + 2.3, bz1], "glass_t", s > 0 ? K0 : {});
    for (let z = bz0 + 0.06; z <= bz1 + 0.01; z += 3) A.box([x - 0.06, dy + 0.01, z - 0.05], [x + 0.06, dy + 2.5, z + 0.05], "panel_dk", L1);
  }
  A.box([bx0 - 0.25, dy + 2.5, bz0 - 0.2], [bx1 + 0.25, dy + 2.7, bz1 + 0.2], "panel_lt", { bv: 0.02 });
  A.box([bx0 + 0.6, dy + 2.48, bz0 + 1], [bx1 - 0.6, dy + 2.5, bz1 - 1], "light_panel", L1);
  for (const z of [2.6, -13.6]) for (const x of [bx0 + 0.25, bx1 - 0.25]) A.box([x - 0.15, gf, z - 0.15], [x + 0.15, dy - 0.3, z + 0.15], "panel_dk");
  for (const [z0, z1] of [[1.4, 3.6], [-14.6, -12.4]]) {
    const n = 17, run = 4.76, mid = gf + 3.0;
    flight(A, { axis: "x", b0: z0, b1: z1, a0: 27.4, dir: -1, y0: gf, rise: 3.0, n, run, m: "precast", soffit: "panel_dk", guard: true });
    A.box([21.14, mid - 0.25, z0], [22.64, mid, z1], "precast");
    for (const zz of [z0 - 0.04, z1]) A.box([21.14, mid, zz], [22.64, mid + 1.1, zz + 0.04], "glass_t");
    A.box([21.1, mid, z0], [21.14, mid + 1.1, z1], "glass_t");   // stops a step off the landing's far end
    flight(A, { axis: "x", b0: z0, b1: z1, a0: 21.14, dir: -1, y0: mid, rise: 3.0, n, run, m: "precast", soffit: "panel_dk", guard: true });
    A.box([21.7, gf, (z0 + z1) / 2 - 0.15], [22.1, mid - 0.25, (z0 + z1) / 2 + 0.15], "panel_dk");   // landing column
  }
  A._nav.cover.push(cover([-20, gf, 2.0], 0, "high", ["left", "right"]), cover([20, gf, -12.0], 180, "high", ["left", "right"]));
  coverRing(A, -12, 12, 4, 18);
  doorTour(A, "entrance_w", -6, 18, 17.7, gf, "mod_station_a_concourse");
  A.meta.tour.start.pos = [-6, 0, 25];
  A.meta.tour.steps.unshift({ goto: [-6, 22.8], note: "up the front steps" });
  return finish(A);
}

// ============================================================================ 6. office towers
// shared: office floors over a lobby, a core with stairs to the roof, a lift, toilets, roof plant
function officeRoof(A, w, xs) {
  const y = w.top;
  hvac(A, xs[0], xs[1], y, 2.4, 1.4); hvac(A, xs[0], xs[1] + 2.4, y, 2.4, 1.4);
  tank(A, xs[2], xs[3], y, 2.0, 2.0);
  plantScreen(A, xs[0] - 1.8, xs[0] + 1.8, xs[1] - 1.2, xs[1] + 3.6, y, 1.6);
  for (let x = w.ix0 + 1.5; x < w.ix1 - 1; x += 3) A.box([x - 0.12, y, w.iz0 + 0.3], [x + 0.12, y + 0.06, w.iz0 + 0.55], "alu", L1);   // roof drains / upstands
}
function towerCommon(A, w, k, L, roofXs) {
  buildCore(A, k, 0, k.ys.slice(0, -1).map((_, f) => `f${f}_corridor`)); buildLift(A, L, k.ys.at(-1));
  officeRoof(A, w, roofXs);
  addRoom(A, "roof", "utility", w.ix0, w.top, w.iz0, w.ix1, w.top + 2.5, w.iz1);
  navDoor(A, `f${k.ys.length - 2}_corridor`, "roof", [k.xa + k.len / 2, w.top, k.zF]);
}

export function towerGlass() {
  const n = 7, h = 3.6, ys = Array.from({ length: n + 1 }, (_, f) => GF + f * h), zc = [-1.0, 1.0];
  const A = mk("mod_bld_tower_glass", "Glass office tower", "Slim seven-storey office tower on a 14 x 14 m plan, wrapped in a dark-framed glass curtain wall with aluminium fins and slab-edge bands. Glass entrance lobby with reception; six office floors (an open-plan floor behind, a cellular office in front) round a core of stairs, lift and toilets; a stair rising to a roof door, a parapet with a metal coping, rooftop air-handling units in a louvred screen and a water tank.",
    7, [14, 14], [0.06, 0.10, 0.12]);
  const k = mkCore({ xa: -6.7, zA: 1.12, ys, encR: true, encTo: 6.7, roof: true, tag: "core" });
  const L = mkLift({ x0: -3.93, x1: -1.63, z0: 1.0, z1: 6.7, door: "rear", ys: ys.slice(0, n) });
  const full = [[-6.4, 6.4, 0.1, 3.2, 4.3]];
  const w = wing(A, {
    tag: "t", x0: -7, x1: 7, z0: -7, z1: 7, n, h, cores: [k], lifts: [L], skin: () => "panel_dk", band: "alu_lt", finMat: "alu_lt", fin: 0.22,
    doors: [{ id: "main_entrance", side: "front", c: 2.5, w: 2.0, h: 2.5, double: true, glass: true, room: "f0_lobby" }],
    glaze: (f) => ({ front: f === 0 ? [[-6.4, 1.3, 0.1, 3.2, 3.9], [3.7, 6.4, 0.1, 3.2]] : [[-6.4, -1.8, 0.1, 3.2], [1.8, 6.4, 0.1, 3.2]], rear: full, east: full, west: full }),
    plan: (f, { w }) => dl(w, { pre: `f${f}_`, zc,
      front: f === 0 ? [{ x1: -3.93, skip: 1 }, { x1: -1.63, skip: 1 }, { id: "lobby", type: "lobby", open: 1 }] : [{ x1: -3.93, skip: 1 }, { x1: -1.63, skip: 1 }, { x1: 1.6, id: "wc", type: "bathroom" }, { id: "office_f", type: "office" }],
      rear: f === 0 ? [{ id: "reception", type: "office", dw: 1.6 }] : [{ id: "office_r", type: "office", open: 1 }] }),
    dress: (A, w) => {
      canopy(A, { x0: 0.5, x1: 4.5, z0: 7, z1: 9.0, y: 3.3, cols: [], m: "alu_lt", fascia: "alu" });
      A.box([0.3, -0.2, 7], [4.7, 0.24, 9.4], "paving", { bv: 0.02 });
      for (const s of SIDES) { const F = sideF(w, s), isX = F.axis === "x"; fbox(A, F, isX ? -7.03 : -6.995, isX ? 7.03 : 6.995, w.top + 1.16, w.top + 1.5, -0.005, 0.03, "alu_lt", L1); }   // crown band
    },
  });
  towerCommon(A, w, k, L, [3.0, -5.0, 3.5, 3.5]);
  coverRing(A, -7, 7, -7, 7);
  doorTour(A, "main_entrance", 2.5, 7, 6.7, GF, "mod_tower_glass_a_lobby");
  return finish(A);
}

export function towerStone() {
  const n = 7, h = 3.6, ys = Array.from({ length: n + 1 }, (_, f) => GF + f * h), zc = [-1.6, 0.4];
  const A = mk("mod_bld_tower_stone", "Stone-cornered office tower", "Slim seven-storey office tower on a 16 x 12 m plan: limestone-clad corner piers and spandrels with projecting pilasters, bronze-framed window bands between, a stone base course and a deep cornice. Lobby and back office on the ground floor; six office floors round a core of stairs, lift and toilets; a stair to a roof door, a parapet, rooftop units and a water tank.",
    7, [16, 12], [0.20, 0.14, 0.09]);
  const k = mkCore({ xa: -7.7, zA: 0.52, ys, encR: true, encTo: 5.7, roof: true, tag: "core" });
  const L = mkLift({ x0: -4.93, x1: -2.63, z0: 0.4, z1: 5.7, door: "rear", ys: ys.slice(0, n) });
  const w = wing(A, {
    tag: "t", x0: -8, x1: 8, z0: -6, z1: 6, n, h, cores: [k], lifts: [L], skin: () => "stone_clad", band: "stone_clad", finMat: "stone_clad", fin: 0.14,
    doors: [{ id: "main_entrance", side: "front", c: 4.3, w: 2.0, h: 2.5, double: true, glass: true, room: "f0_lobby" }],
    glaze: (f) => ({ front: f === 0 ? [[-5.9, -3.0, 0.2, 3.1], [-2.4, 3.1, 0.2, 3.1]] : [[-5.9, -3.0, 0.8, 3.0], [0.8, 5.9, 0.8, 3.0, 2.6]], rear: [[-5.9, 5.9, 0.8, 3.0, 3.9]], east: [[-3.9, 3.9, 0.8, 3.0]], west: [[-3.9, 3.9, 0.8, 3.0]] }),
    plan: (f, { w }) => dl(w, { pre: `f${f}_`, zc,
      front: f === 0 ? [{ x1: -4.93, skip: 1 }, { x1: -2.63, skip: 1 }, { id: "lobby", type: "lobby", open: 1 }] : [{ x1: -4.93, skip: 1 }, { x1: -2.63, skip: 1 }, { x1: 0.5, id: "wc", type: "bathroom" }, { id: "office_f", type: "office" }],
      rear: f === 0 ? [{ id: "office", type: "office" }] : [{ id: "office_r", type: "office", open: 1 }] }),
    dress: (A, w) => {
      for (const s of SIDES) {
        const F = sideF(w, s), isX = F.axis === "x", [a0, a1] = isX ? [-8, 8] : [-6, 6];
        for (const a of [a0 + 0.9, a1 - 0.9]) fbox(A, F, a - 0.75, a + 0.75, 0.6, w.top + 1.1, -0.005, 0.07, "stone_clad", { k: 0, lod: 1, bv: 0.01 });   // corner pilasters
        fbox(A, F, isX ? a0 - 0.12 : a0 + 0.005, isX ? a1 + 0.12 : a1 - 0.005, -0.3, 0.6, -0.005, 0.12, "precast", { k: 0, bv: 0.015 });                 // base course
        fbox(A, F, isX ? a0 - 0.32 : a0 + 0.005, isX ? a1 + 0.32 : a1 - 0.005, w.top - 0.45, w.top - 0.1, -0.005, 0.32, "stone_clad", { k: 0, bv: 0.02 });   // cornice
        fbox(A, F, isX ? a0 - 0.2 : a0 + 0.005, isX ? a1 + 0.2 : a1 - 0.005, w.top - 0.65, w.top - 0.45, -0.005, 0.2, "stone_clad", { k: 0, lod: 1, bv: 0.015 });
      }
      canopy(A, { x0: 2.6, x1: 6.0, z0: 6, z1: 7.8, y: 3.3, cols: [], m: "bronze", fascia: "bronze" });
      A.box([2.4, -0.2, 6], [6.2, 0.24, 8.0], "paving", { bv: 0.02 });
    },
  });
  towerCommon(A, w, k, L, [3.2, -4.4, 4.5, 3.5]);
  coverRing(A, -8, 8, -6, 6);
  doorTour(A, "main_entrance", 4.3, 6, 5.7, GF, "mod_tower_stone_a_lobby");
  return finish(A);
}

export function officeWide() {
  const n = 5, h = 3.6, ys = Array.from({ length: n + 1 }, (_, f) => GF + f * h), zc = [-1.2, 1.2];
  const A = mk("mod_bld_office_wide", "Office block (wide)", "Five-storey rectangular office block, 36 x 18 m: continuous ribbon windows between precast concrete spandrels, horizontal aluminium sun louvres over the windows, a dark slab-edge band. A glazed lobby, offices and a glass-walled meeting room on the ground floor; office floors either side of a central corridor above; a core of stairs, lift and toilets with a stair to a roof door, a parapet, rooftop units in a louvred screen and a water tank.",
    5, [36, 20], [0.12, 0.13, 0.14]);
  const k = mkCore({ xa: -17.7, zA: 1.32, ys, encR: true, encTo: 8.7, roof: true, tag: "core" });
  const L = mkLift({ x0: -14.93, x1: -12.63, z0: 1.2, z1: 8.7, door: "rear", ys: ys.slice(0, n) });
  const w = wing(A, {
    tag: "o", x0: -18, x1: 18, z0: -9, z1: 9, n, h, cores: [k], lifts: [L], skin: () => "precast", band: "panel_dk", fin: 0.08,
    doors: [{ id: "main_entrance", side: "front", c: -2.75, w: 2.4, h: 2.5, double: true, glass: true, room: "f0_lobby" }],
    glaze: (f) => ({ front: f === 0 ? [[-9.2, -4.2, 0.1, 3.0], [-1.3, 3.7, 0.1, 3.0], [4.3, 17.2, 0.9, 2.9, 4.3]] : [[-9.2, 17.2, 0.9, 2.9, 4.4]], rear: [[-17.2, 17.2, 0.9, 2.9, 4.3]], east: [[-6.5, 6.5, 0.9, 2.9, 4.3]], west: [[-8.2, -1.8, 0.9, 2.9, 3.2]] }),
    plan: (f, { w }) => dl(w, { pre: `f${f}_`, zc,
      front: f === 0 ? [{ x1: -14.93, skip: 1 }, { x1: -12.63, skip: 1 }, { x1: -9.5, id: "wc", type: "bathroom" }, { x1: 4, id: "lobby", type: "lobby", open: 1 }, { id: "office_1", type: "office" }]
        : [{ x1: -14.93, skip: 1 }, { x1: -12.63, skip: 1 }, { x1: -9.5, id: "wc", type: "bathroom" }, { x1: 0, id: "office_a", type: "office" }, { x1: 8.5, id: "office_b", type: "office" }, { id: "office_c", type: "office" }],
      rear: f === 0 ? [{ x1: -6, id: "office_2", type: "office" }, { x1: 0, id: "meeting", type: "office", m: "glass_int" }, { x1: 9, id: "office_3", type: "office" }, { id: "office_4", type: "office" }]
        : [{ x1: -6, id: "office_d", type: "office" }, { x1: 0, id: "meeting", type: "office", m: "glass_int" }, { id: "office_e", type: "office", open: 1 }] }),
    dress: (A, w) => {
      for (let f = 0; f < n; f++) for (const s of ["front", "rear"]) for (const [a0, a1] of (w.glaze(f)[s] ?? []).filter((g) => g[2] > 0.5).map((g) => [g[0], g[1]])) {
        const F = sideF(w, s), y = GF + f * h + 2.9;
        fbox(A, F, a0 - 0.1, a1 + 0.1, y + 0.1, y + 0.14, 0, 0.6, "alu_lt", L1);
        fbox(A, F, a0 - 0.1, a1 + 0.1, y + 0.32, y + 0.36, 0, 0.45, "alu_lt", L1);
      }
      for (const s of ["east", "west"]) { const F = sideF(w, s); for (let a = -8; a <= 8; a += 2) fbox(A, F, a - 0.04, a + 0.04, 0.3, w.top + 1.1, 0, 0.075, "panel_dk", L1); }
      canopy(A, { x0: -5.5, x1: 0, z0: 9, z1: 11.2, y: 3.3, cols: [[-5.1, 10.8], [-0.4, 10.8]], m: "panel_lt", fascia: "panel_dk" });
      A.box([-5.5, -0.2, 9], [0, 0.24, 11.2], "paving", { bv: 0.02 });
    },
  });
  towerCommon(A, w, k, L, [8, -6.5, 13.5, 4.5]);
  coverRing(A, -18, 18, -9, 9);
  doorTour(A, "main_entrance", -2.75, 9, 8.7, GF, "mod_office_wide_a_lobby");
  return finish(A);
}

export function officeSetback() {
  const h = 3.6, ys = Array.from({ length: 7 }, (_, f) => GF + f * h), zc = [-1.2, 1.2], topY = GF + 5 * h;
  const A = mk("mod_bld_office_setback", "Office block (setback top)", "Six-storey office block, 30 x 20 m: five floors of grey brick with ribbon windows and dark bands, topped by a fully glazed sixth floor set back 4 m at each end behind roof terraces with glass balustrades, paving and planters. Lobby and offices below, office floors with glass-walled meeting rooms, a boardroom and lounge on the top floor opening onto both terraces; a core of stairs, lift and toilets, the stair rising to a roof door; rooftop units and a water tank.",
    6, [30, 20], [0.07, 0.08, 0.09]);
  const k = mkCore({ xa: -10.7, zA: 1.32, ys, encL: true, encR: true, encTo: 9.7, roof: true, tag: "core" });
  const L = mkLift({ x0: -7.93, x1: -5.63, z0: 1.2, z1: 9.7, door: "rear", ys: ys.slice(0, 6) });
  const base = wing(A, {
    tag: "b", x0: -15, x1: 15, z0: -10, z1: 10, n: 5, h, cores: [k], lifts: [L], skin: () => "brick_grey", band: "panel_dk", fin: 0.1, par: 0.5,
    roofHoles: [[-10.7, -9.7, 10.7, 9.7]], parSkip: { front: [[-11, 11]], rear: [[-11, 11]] },
    doors: [{ id: "main_entrance", side: "front", c: 0, w: 2.4, h: 2.5, double: true, glass: true, room: "f0_lobby" }],
    glaze: (f) => ({ front: f === 0 ? [[-14.4, -11.2, 0.9, 3.0], [-5.3, -1.5, 0.1, 3.0], [1.5, 5.7, 0.1, 3.0], [6.3, 14.4, 0.9, 3.0, 4.1]] : [[-14.4, -11.2, 0.9, 3.0], [-2.2, 14.4, 0.9, 3.0, 5.5]],
      rear: [[-14.4, 14.4, 0.9, 3.0, 5.8]], east: [[-8, 8, 0.9, 3.0, 5.3]], west: [[-8, 8, 0.9, 3.0, 5.3]] }),
    plan: (f, { w }) => dl(w, { pre: `f${f}_`, zc,
      front: f === 0 ? [{ x1: -10.85, id: "office_w", type: "office" }, { x1: -7.93, skip: 1 }, { x1: -5.63, skip: 1 }, { x1: 6, id: "lobby", type: "lobby", open: 1 }, { id: "office_e", type: "office" }]
        : [{ x1: -10.85, id: "office_w", type: "office" }, { x1: -7.93, skip: 1 }, { x1: -5.63, skip: 1 }, { x1: -2.5, id: "wc", type: "bathroom" }, { x1: 6, id: "office_a", type: "office" }, { id: "office_b", type: "office" }],
      rear: f === 0 ? [{ x1: -5, id: "office_r1", type: "office" }, { x1: 2, id: "meeting", type: "office", m: "glass_int" }, { id: "office_r2", type: "office" }]
        : [{ x1: -4, id: "office_c", type: "office" }, { x1: 2, id: "meeting", type: "office", m: "glass_int" }, { id: "office_d", type: "office", open: 1 }] }),
    dress: (A, w) => {
      canopy(A, { x0: -2.6, x1: 2.6, z0: 10, z1: 12.2, y: 3.3, cols: [[-2.2, 11.8], [2.2, 11.8]], m: "panel_lt", fascia: "panel_dk" });
      A.box([-2.6, -0.2, 10], [2.6, 0.24, 12.2], "paving", { bv: 0.02 });
      // terraces: paving, glass balustrade on the parapet, planters
      for (const s of [-1, 1]) {
        const [a, b] = s < 0 ? [-14.7, -11] : [11, 14.7];
        A.box([a, topY, -9.7], [b, topY + 0.04, 9.7], "paving");
        for (const z of [-7, 0.5, 7]) { A.box([s < 0 ? a + 0.1 : b - 1.1, topY + 0.04, z - 1.4], [s < 0 ? a + 1.1 : b - 0.1, topY + 0.6, z + 1.4], "precast", { bv: 0.02 }); A.box([s < 0 ? a + 0.18 : b - 1.02, topY + 0.6, z - 1.32], [s < 0 ? a + 1.02 : b - 0.18, topY + 0.62, z + 1.32], "soil", K0); A.cone([s < 0 ? a + 0.6 : b - 0.6, topY + 1.2, z], [0.9, 1.3, 2.4], "leaf", { k: 0, top: 0.3, n: 8 }); }
      }
      for (const [x0, x1, z0, z1] of [[-15, -11, 9.68, 9.74], [11, 15, 9.68, 9.74], [-15, -11, -9.74, -9.68], [11, 15, -9.74, -9.68], [-14.88, -14.82, -9.68, 9.68], [14.82, 14.88, -9.68, 9.68]]) A.box([x0, topY + 0.56, z0], [x1, topY + 1.15, z1], "glass_t");
    },
  });
  const top = wing(A, {
    tag: "u", x0: -11, x1: 11, z0: -10, z1: 10, n: 1, h, gf: topY, base: false, cores: [k], lifts: [L], skin: () => "panel_dk", band: null, finMat: "alu_lt", fin: 0.2,
    doors: [{ id: "terrace_door_w", side: "west", c: 0, w: 1.8, h: 2.4, double: true, glass: true, room: "f5_corridor", to: "terrace_w" },
      { id: "terrace_door_e", side: "east", c: 0, w: 1.8, h: 2.4, double: true, glass: true, room: "f5_corridor", to: "terrace_e" }],
    glaze: () => ({ front: [[-10.4, -5.9, 0.1, 3.2], [-2.2, 10.4, 0.1, 3.2, 4.2]], rear: [[-10.4, 10.4, 0.1, 3.2, 4.2]], west: [[-9.4, -1.3, 0.1, 3.2, 4.05], [1.3, 9.4, 0.1, 3.2, 4.05]], east: [[-9.4, -1.3, 0.1, 3.2, 4.05], [1.3, 9.4, 0.1, 3.2, 4.05]] }),
    plan: (f, { w }) => dl(w, { pre: "f5_", zc,
      front: [{ x1: -7.93, skip: 1 }, { x1: -5.63, skip: 1 }, { x1: -2.5, id: "wc", type: "bathroom" }, { id: "boardroom", type: "office" }],
      rear: [{ id: "lounge", type: "office", open: 1 }] }),
  });
  for (const s of [-1, 1]) addRoom(A, s < 0 ? "terrace_w" : "terrace_e", "utility", s < 0 ? -14.7 : 11, topY, -9.7, s < 0 ? -11 : 14.7, topY + 2.4, 9.7);
  void base;
  towerCommon(A, top, k, L, [4, -6.5, 6.5, 5.5]);
  coverRing(A, -15, 15, -10, 10);
  doorTour(A, "main_entrance", 0, 10, 9.7, GF, "mod_office_setback_a_lobby");
  return finish(A);
}

export function modernBuildingsB() {
  return [school(), hospital(), leisureCentre(), bigBox(), cartCorral(), trainStation(), towerGlass(), towerStone(), officeWide(), officeSetback()];
}
