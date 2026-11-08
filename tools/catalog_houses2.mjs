// B2 (docs/asset-format.md#b1-geometry-kit): every existing house, outbuilding, town building and the village church
// rebuilt with the B1 geometry kit. Same ids, version 2, so placed copies update in place. The interiors, stairs, doors,
// casements, shutters, damage bays, nav and tours are the legacy builders' own (moved here from build_assets.mjs /
// catalog_village2.mjs, or imported from catalog_small / catalog_large); dress() layers the facades over them:
//  - a chamfered plinth course, string courses at the floors, a stepped cornice under the eaves;
//  - bevelled quoins and opening dressings (lintels, sills, thresholds, steps), jambs and keystones round the
//    openings (slim ones where shutters fold back over them), full surrounds where there are none;
//  - per building: half-timbering on the Norman types, arches (inn arcade, church portal and windows, cart arches,
//    municipal portico), balconies, dormers, capped chimneys;
//  - ivy on one or two faces, B1 weathering (trim jitter, chips, shell scars on the gables);
//  - a seeded set of companions (catalog_dressing.mjs pieces, shrubs, hawthorn, flowers) placed with the building.
// Facade members are visual only (k: 0) unless they carry people (balconies, arcades); the walls stay the bays.
// Regenerate with `node tools/build_mpu_assets.mjs` (never run build_assets.mjs).
import { Asset, cutRect, wbox } from "./kit.mjs";
import {
  stdMaterials, wall, partition, window, door, stairs, railing, table, chair, bench, bed, armoire, dresser, chest, gableRoof, quoins,
  pilaster, stringCourse, cornice, surround, archOpening, colonnade, balcony, dormer, turret, timberFrame, chimney, chimneyCap, fbox, fpart, weather,
} from "./parts.mjs";
import { workshop, dairy, storehouse, office } from "./catalog_small.mjs";
import { schoolhouse, inn, warehouse, municipal } from "./catalog_large.mjs";
import { normanFarmhouse, stoneBarn, roofX, pews, pnM, unglazed, PT } from "./catalog_village2.mjs";
import { rng, fire, cover, room, corners, setNav, setTour, r3 } from "./catalog_atlantic.mjs";

const q4 = (v) => Math.round(v * 1e4) / 1e4;
const SD = "stone_dressed";

// ============================================================================ part-list surgery
function eachList(A, fn) { fn(A.parts); for (const p of A.panels) for (const st of p.states) fn(st); }
const bounds = (p) => [0, 1, 2].map((i) => [p.c[i] - p.s[i] / 2, p.c[i] + p.s[i] / 2]);
function drop(A, pred) { const keep = A.parts.filter((p) => !pred(p)); A.parts.length = 0; A.parts.push(...keep); }
// boxes of `from` (static and in every bay state) whose x / z lie inside [min, max]: those crossing y = `above` are split
// there, the parts above take `to` (a rendered upper storey for the half-timbering)
function remat(A, { min, max, above, from = "stone", to = "render" }) {
  eachList(A, (L) => {
    const out = [];
    for (const p of L) {
      if (p.m !== from || p.t !== "b" || p.r) { out.push(p); continue; }
      const b = bounds(p);
      if (![0, 2].every((i) => b[i][0] >= min[i] - 1e-4 && b[i][1] <= max[i] + 1e-4) || b[1][1] <= above + 1e-4) { out.push(p); continue; }
      if (b[1][0] < above - 1e-4) {
        const lo = { ...p, c: [...p.c], s: [...p.s] }, hi = { ...p, c: [...p.c], s: [...p.s], m: to };
        lo.s[1] = q4(above - b[1][0]); lo.c[1] = q4((b[1][0] + above) / 2);
        hi.s[1] = q4(b[1][1] - above); hi.c[1] = q4((above + b[1][1]) / 2);
        out.push(lo, hi);
      } else { p.m = to; out.push(p); }
    }
    L.length = 0; L.push(...out);
  });
}

// ============================================================================ the facade dresser
// a box building |x| <= X, |z| <= Z, walls T thick, ridge along X. Faces "+z" (front), "-z", "+x", "-x".
const FACES = (X, Z) => ({ "+z": { axis: "x", d: Z, out: 1, a0: -X, a1: X }, "-z": { axis: "x", d: -Z, out: -1, a0: -X, a1: X },
  "+x": { axis: "z", d: X, out: 1, a0: -Z, a1: Z }, "-x": { axis: "z", d: -X, out: -1, a0: -Z, a1: Z } });
// exterior doors and windows from the interactables: the casement / door apertures give the hole, a "_shutters"
// sibling marks shutters that fold back over the jambs
function openingsOf(A, X, Z, T) {
  const out = [];
  for (const it of A.interactables) {
    if (!it.aperture || (it.kind !== "door" && it.kind !== "window")) continue;
    const { min, max } = it.aperture, n = max[0] - min[0] < max[2] - min[2] ? 0 : 2, u = 2 - n;
    const sgn = Math.sign(min[n] + max[n]) || 1, inner = sgn > 0 ? min[n] : max[n], half = n === 2 ? Z : X;
    if (Math.abs(Math.abs(inner) + T - half) > 0.06) continue;
    const face = (sgn > 0 ? "+" : "-") + (n === 2 ? "z" : "x"), base = it.id.replace(/_casement$/, "");
    out.push({ id: it.id, kind: it.kind, face, a0: min[u], a1: max[u], y0: min[1], y1: max[1], double: (it.leaves || []).length === 2 && it.kind === "door",
      shut: A.interactables.some((s) => s.id === base + "_shutters") });
  }
  return out;
}
// slim jambs and a keystone (shutters fold over the jambs: under 2 cm proud), a full surround, or door jambs
function frameOpening(A, F, o) {
  const { a0, a1, y0, y1 } = o, ac = (a0 + a1) / 2;
  if (o.kind === "window" && o.shut) {
    for (const [p0, p1] of [[a0 - 0.12, a0 - 0.004], [a1 + 0.004, a1 + 0.12]]) fbox(A, F, p0, p1, y0 - 0.01, y1, -0.01, 0.014, SD, { bv: 0.005, k: 0 });
    fbox(A, F, ac - 0.1, ac + 0.1, y1 + 0.045, y1 + 0.31, -0.01, 0.055, SD, { bv: 0.012, k: 0 });
  } else if (o.kind === "window") surround(A, F, { a0, a1, y0, y1, w: 0.13, key: true });
  else {
    for (const [p0, p1] of [[a0 - 0.15, a0 - 0.004], [a1 + 0.004, a1 + 0.15]]) fbox(A, F, p0, p1, y0 - 0.01, y1, -0.01, 0.05, SD, { bv: 0.01, k: 0 });
    if (o.double) archOpening(A, F, { a0: a0 - 0.05, a1: a1 + 0.05, y1: y1 + 0.03, rise: Math.min(0.65, (a1 - a0) * 0.2), w: 0.24, proj: 0.06 });   // cart arch over the lintel
    else fbox(A, F, ac - 0.11, ac + 0.11, y1 + 0.05, y1 + 0.35, -0.01, 0.06, SD, { bv: 0.012, k: 0 });
  }
}
function band(A, F, a0, a1, y, h, proj, holes, m = SD, bv = 0.01) {
  for (const [p0, p1] of cutRect(a0, a1, y, y + h, holes)) if (p1 - p0 > 0.12) fbox(A, F, p0, p1, y, y + h, -0.01, proj, m, { bv, k: 0 });
}
// Dormer on the gable roof's slope (ridge along X, pitch tn) with its front 0.55 m in from the eave line
function roofDormer(A, S, x, s = 1, w = 1.0, h = 1.0) {
  const tn = (S.RIDGE - S.EAVE) / S.Z, cs = Math.cos(Math.atan(tn)), zf = S.Z - 0.55, y = S.EAVE + 0.14 / cs + 0.55 * tn - 0.02;
  const rh = (w / 2 + 0.15) * 0.9, depth = (h + rh + 0.12) / tn + 0.15;
  dormer(A, { x, z: s * zf, y, w, h, depth, s });
}
// S: { X, Z, T, GF, EAVE, RIDGE, floors (heights of the upper floors), stringDrop, timber: { face: { above, rails, proj } },
//      cornice: [faces], plinth, ivy, dormers: [[x, s, w, h]], chimneys: [{x, z, ...}], extra(A, ctx), pocks, yard }
function dress(A, S) {
  const { X, Z, T, GF, EAVE } = S, Fs = FACES(X, Z), y0 = GF - 0.02;
  const ops = openingsOf(A, X, Z, T), on = (f) => ops.filter((o) => o.face === f);
  // bevel the legacy opening dressings (lintels, sills, thresholds, steps, the hearth jambs stay plain) and the quoins
  const legacy = [];
  for (const p of A.parts) {
    if (p.t !== "b" || p.r || p.bv || p.m !== SD || p.k === 0 || p.v === 0) continue;
    const hs = [p.s[0], p.s[2]].sort((a, b) => a - b), quoin = Math.abs(hs[0] - 0.335) < 0.002 && Math.abs(hs[1] - 0.565) < 0.002 && p.s[1] < 0.295;
    if (quoin || (p.s[1] <= 0.42 && Math.max(...p.s) < 4)) { p.bv = quoin ? 0.01 : 0.012; legacy.push(p); }
  }
  // half-timbering: the stone above `above` on that face becomes render, the frame goes over it
  for (const [f, t] of Object.entries(S.timber || {})) {
    const F = Fs[f], dIn = F.d - F.out * (T - 0.02);   // the stone layer: the outer face in to 3 cm off the plaster
    if (t.render !== false) remat(A, { min: F.axis === "x" ? [-X - 0.1, 0, Math.min(dIn, F.d)] : [Math.min(dIn, F.d), 0, -Z - 0.1], max: F.axis === "x" ? [X + 0.1, 0, Math.max(dIn, F.d)] : [Math.max(dIn, F.d), 0, Z + 0.1], above: t.above });
    const holes = on(f).filter((o) => o.y1 > t.above).map((o) => ({ a0: o.a0, a1: o.a1, y0: o.kind === "door" ? t.above : o.y0 - 0.09, y1: o.y1 + (o.kind === "door" ? 0.27 : 0.25) }));
    timberFrame(A, F, { a0: F.a0, a1: F.a1, y0: t.above, y1: EAVE, holes, rails: t.rails || [], proj: t.proj ?? 0.018, sp: t.sp ?? 0.8 });
  }
  // plinth course round the base (cut for the door steps); the long faces own the corners
  if (S.plinth !== false) for (const [f, F] of Object.entries(Fs)) {
    const long = F.axis === "x", top = y0 + 0.08 - (long ? 0 : 0.006), holes = on(f).filter((o) => o.kind === "door").map((o) => ({ a0: o.a0 - 0.27, a1: o.a1 + 0.27, y0: -9, y1: 9 }));
    const e = long ? 0.09 : 0;
    for (const [p0, p1] of cutRect(F.a0 - e, F.a1 + e, -0.3, top, holes)) {
      if (p1 - p0 < 0.15) continue;
      fbox(A, F, p0, p1, -0.3, top, -0.01, 0.08, "stone_plinth", { bv: 0.02, k: 0 });
      fpart(A, "wedge", F, (p0 + p1) / 2, top + 0.025, 0.035, [p1 - p0 - 0.02, 0.05, 0.08], "stone_plinth", { top: 0.2, k: 0 });
    }
  }
  // string courses at the upper floors, cut round the openings
  for (const fy of S.floors || []) {
    const y = fy - (S.stringDrop ?? 0.18);
    for (const [f, F] of Object.entries(Fs)) {
      if (S.timber?.[f] && S.timber[f].above < y) continue;
      const holes = on(f).map((o) => ({ a0: o.a0 - 0.16, a1: o.a1 + 0.16, y0: o.y0 - 0.12, y1: o.y1 + 0.36 })), long = F.axis === "x";
      band(A, F, F.a0 - (long ? 0.06 : 0), F.a1 + (long ? 0.06 : 0), y, long ? 0.16 : 0.15, 0.06, holes);
    }
  }
  // the openings' own dressings (the timber frame stands in for them on half-timbered storeys)
  for (const o of ops) if (!(S.timber?.[o.face] && o.y0 > S.timber[o.face].above - 0.3)) frameOpening(A, Fs[o.face], o);
  // stepped cornice on the eaves walls, clear of the window heads
  for (const f of S.cornice ?? ["+z", "-z"]) {
    if (S.timber?.[f]) continue;
    const yTop = Math.max(EAVE - 0.5, ...on(f).map((o) => o.y1 + 0.36)), h = EAVE - 0.24 - yTop;
    if (h >= 0.14) cornice(A, Fs[f], -X, X, yTop, { steps: h < 0.26 ? 2 : 3, h, proj: 0.16 });
  }
  const ctx = { Fs, ops, on, y0 };
  S.extra?.(A, ctx);
  for (const [x, s, w, h] of S.dormers || []) roofDormer(A, S, x, s, w, h);
  for (const c of S.chimneys || []) chimney(A, { z: 0, y0: S.RIDGE - 0.6, y1: S.RIDGE + 1.3, w: 0.8, d: 0.55, ...c });
  if (S.ivy) A.meta.ivy = S.ivy;
  A.meta.version = 2;
  A.meta.description += " " + (S.note || "Dressed-stone plinth, quoins, opening surrounds and cornice; ivy; capped chimney.");
  const tn = (S.RIDGE - EAVE) / Z;
  weather(A, { keep: legacy, pocks: S.pocks === false ? [] : [-1, 1].map((s) => ({ F: { axis: "z", d: s * X, out: s }, a0: -Z * 0.35, a1: Z * 0.35, y0: EAVE + 0.15, y1: EAVE + Math.min(0.42 * tn * Z, 3), n: 2 })) });
  A.meta.companions = yard(A, S, ops);
  return A;
}

// ============================================================================ companions
const FLOWERS = ["nat_flowers_poppy", "nat_flowers_daisy", "nat_flowers_foxglove", "nat_flowers_cornflower", "nat_flowers_buttercup", "nat_flowers_cowparsley"];
function seedOf(s) { let h = 2166136261; for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 16777619); } return h >>> 0; }
// The kind picks the set; positions keep 1.5 m off the walls and clear of the door approaches. Each piece has a chance,
// rolled per placed copy (App::placeCompanions), so no two copies are dressed alike; wall / fence runs always come.
function yard(A, S, ops) {
  const { X, Z } = S, R = rng(seedOf(A.meta.id)), list = [];
  const fd = ops.filter((o) => o.face === "+z" && o.kind === "door").map((o) => (o.a0 + o.a1) / 2), rd = ops.filter((o) => o.face === "-z" && o.kind === "door").map((o) => (o.a0 + o.a1) / 2);
  const clear = (x, z) => ![...fd.map((d) => [d, 1]), ...rd.map((d) => [d, -1])].some(([d, s]) => Math.abs(x - d) < 1.7 && z * s > 0 && Math.abs(z) > Z);
  const add = (asset, x, z, yaw = 0, chance = 0.8, jit = 0.35) => {
    const p = [x + (R() - 0.5) * jit, z + (R() - 0.5) * jit];
    if (!clear(p[0], p[1])) return;
    list.push({ asset, pos: [r3(p[0]), 0, r3(p[1])], yaw: Math.round(yaw + (R() - 0.5) * 2 * jit * 20), ground: true, ...(chance < 1 ? { chance } : {}) });
  };
  const flower = () => FLOWERS[Math.floor(R() * FLOWERS.length)];
  const gfWin = ops.filter((o) => o.face === "+z" && o.kind === "window" && o.y0 < 2).map((o) => (o.a0 + o.a1) / 2);
  const k = S.yard || "farm";
  if (k !== "church") {
    for (const x of gfWin.slice(0, k === "town" ? 4 : 2)) if (S.beds !== false) add("drs_flower_bed", x, Z + 0.62, 0, 0.7, 0.05);
    add(flower(), -X - 0.9, Z + 1.0, 0, 0.6); add(flower(), X + 0.9, Z + 1.1, 0, 0.6);
    add("nat_hawthorn_bush", -X - 2.6, Z + 2.2, 0, 0.75); add("shrub_hazel", X + 2.3, -Z - 2.3, 0, 0.65);
  }
  if (k === "farm") {
    if (S.well !== false) add("drs_well", X + 3.1, Z + 2.4, 0, 0.75, 0.6);
    add("drs_woodpile", -X - 1.35, -Z * 0.35, -90, 0.85, 0.2);
    add("drs_hay_bale_stack", -X * 0.35, -Z - 2.5, 180, 0.8, 0.5);
    add("drs_cart", X + 3.3, -Z * 0.25, 80, 0.6, 0.6);
    add("drs_barrels", (rd[0] ?? X - 2) + 1.7, -Z - 1.0, 180, 0.7, 0.2);
    add("nat_hawthorn_bush", X + 2.4, -Z - 2.6, 0, 0.6); add(flower(), -X - 2.0, -Z - 1.6, 0, 0.6);
  } else if (k === "cottage") {
    for (let x = -X - 0.5; x < X + 1.0; x += 3.0) if (!fd.some((d) => Math.abs(x - d) < 2.4)) add("drs_picket_fence", x, Z + 3.2, 0, 1, 0);
    for (const d of fd) for (const s of [-1, 1]) add("drs_flower_bed", d + s * 1.6, Z + 2.1, 0, 0.7, 0.05);
    add("drs_woodpile", -X - 1.35, -Z * 0.3, -90, 0.85, 0.2);
    add(flower(), X + 1.4, -Z * 0.2, 0, 0.6);
  } else if (k === "town" || k === "work") {
    add("drs_barrels", (rd[0] ?? 0) + (R() < 0.5 ? -1.9 : 1.9), -Z - 1.0, 180, 0.75, 0.2);
    if (S.cart) add("drs_cart", X + 3.2, Z * 0.2, 95, 0.6, 0.5);
    if (S.sandbags) { add("drs_sandbag_wall", fd[0] ?? 0, Z + 3.3, 0, 0.45, 0.1); add("drs_sandbag_ring", X + 3.4, Z + 3.4, 45, 0.3, 0.3); }
    if (S.xobst) add("drs_wooden_x_obstacle", (fd[0] ?? 0) + 4.0, Z + 7.5, 10, 0.35, 0.8);
    if (S.woodpile) add("drs_woodpile", -X - 1.35, -Z * 0.3, -90, 0.8, 0.2);
    if (S.fence) for (let x = -X + 1.5; x < X - 1.0; x += 3.0) if (!fd.some((d) => Math.abs(x - d) < 2.4)) add("drs_picket_fence", x, Z + 4.0, 0, 1, 0);
    add(flower(), -X - 1.2, -Z - 1.2, 0, 0.5);
  }
  if (S.gardenWall) {   // a walled yard across the front with the gate on the main door
    const gx = fd[0] ?? 0, zw = Z + S.gardenWall;
    list.push({ asset: "drs_gate_piers", pos: [r3(gx), 0, r3(zw)], yaw: 0, ground: true });
    for (const s of [-1, 1]) for (let i = 0; i < 2; i++) list.push({ asset: "drs_garden_wall", pos: [r3(gx + s * (1.9 + 2 + 4 * i)), 0, r3(zw)], yaw: 0, ground: true });
  }
  S.more?.(add, list, R);
  return list;
}

// ============================================================================ legacy builders moved from build_assets.mjs
// =============================================================================
// Two-storey Norman stone farmhouse (corps de ferme), 12 x 9 m.
// Ground floor: kitchen with hearth (west), hall/front room (east).
// Upper floor: two bedrooms and a landing.
// Circulation: a primary stair in the hall and a separate rear service stair
// against the east gable, giving two independent routes between floors.
// =============================================================================
function farmhouseBase() {
  const A = new Asset({
    id: "house_farmhouse_2story", version: 1, name: "Two-Story Farmhouse", category: "Houses",
    description: "Rubble-limestone Norman farmhouse with slate roof. Kitchen with open hearth, hall, two bedrooms. Front stair in the hall plus a rear service stair: two independent routes between floors. Shuttered casement windows on both floors.",
    floors: 2, enterable: true, footprint: [12.2, 9.2], plinthDepth: 1.2, maxGrade: 1.0,
  });
  stdMaterials(A);
  const X = 6, Z = 4.5, T = 0.5, XI = X - T, ZI = Z - T;
  const GF = 0.3, FF = 3.3, EAVE = 6.3, RIDGE = 8.8, CEIL = 6.1;
  const y0 = GF - 0.02;

  // ---- plinth / socle (also the foundation skirt for uneven ground)
  A.box([-X - 0.05, -1.2, -Z - 0.05], [X + 0.05, y0, Z + 0.05], "stone_plinth");
  A.box([-XI, y0, -ZI], [XI, GF, ZI], "terracotta");

  // ---- openings
  const front = { axis: "x", outer: Z, inner: ZI };
  const back = { axis: "x", outer: -Z, inner: -ZI };
  const holesFront = [], holesBack = [];
  holesFront.push(door(A, { id: "front_door", ...front, c: 1.5, floor: GF, w: 1.0, h: 2.1, hingeSide: +1 }));
  holesFront.push(window(A, { id: "kitchen", ...front, c: -3.0, sill: GF + 0.9, w: 1.0, h: 1.3, shuttersOpen: false }));
  holesFront.push(window(A, { id: "parlour", ...front, c: 4.0, sill: GF + 0.9, w: 1.0, h: 1.3 }));
  holesFront.push(window(A, { id: "bedroom_w_front", ...front, c: -3.0, sill: FF + 0.9, w: 1.0, h: 1.25 }));
  holesFront.push(window(A, { id: "bedroom_e_front", ...front, c: 3.6, sill: FF + 0.9, w: 1.0, h: 1.25 }));
  holesBack.push(door(A, { id: "rear_door", ...back, c: 3.2, floor: GF, w: 1.0, h: 2.05, hingeSide: -1 }));
  holesBack.push(window(A, { id: "kitchen_rear", ...back, c: -4.2, sill: GF + 0.9, w: 0.9, h: 1.2 }));
  holesBack.push(window(A, { id: "hall_rear", ...back, c: 1.0, sill: GF + 0.9, w: 1.0, h: 1.3 }));
  holesBack.push(window(A, { id: "bedroom_w_rear", ...back, c: -3.0, sill: FF + 0.9, w: 1.0, h: 1.25 }));
  holesBack.push(window(A, { id: "bedroom_e_rear", ...back, c: 1.5, sill: FF + 0.9, w: 1.0, h: 1.25 }));

  // ---- exterior walls (front/back run the full width; gables sit between them)
  const pn = (side) => ({ side, family: "masonry", bands: [[y0, FF - 0.2, 0, GF], [FF - 0.2, EAVE, 1, FF]] });
  wall(A, { ...front, aOut: [-X, X], aIn: [-XI, XI], y0, y1: EAVE, holes: holesFront, panel: pn("front") });
  wall(A, { ...back, aOut: [-X, X], aIn: [-XI, XI], y0, y1: EAVE, holes: holesBack, panel: pn("rear") });
  wall(A, { axis: "z", outer: X, inner: XI, aOut: [-ZI - 0.03, ZI + 0.03], aIn: [-ZI, ZI], y0, y1: EAVE, holes: [], panel: pn("east") });
  wall(A, { axis: "z", outer: -X, inner: -XI, aOut: [-ZI - 0.03, ZI + 0.03], aIn: [-ZI, ZI], y0, y1: EAVE, holes: [], panel: pn("west") });
  // gables
  for (const s of [-1, 1]) A.prism(0, s > 0 ? XI + 0.03 : -X, s > 0 ? X : -XI - 0.03, [[-Z, EAVE], [Z, EAVE], [0, RIDGE]], "stone");
  // dressed quoins at the four corners
  for (const sx of [-1, 1]) for (const sz of [-1, 1]) {
    for (let i = 0, y = y0; y < EAVE - 0.1; y += 0.31, i++) {
      const longX = i % 2 === 0;
      const lx = longX ? 0.55 : 0.32, lz = longX ? 0.32 : 0.55;
      const xa = sx * (X + 0.015), xb = sx * (X - lx), za = sz * (Z + 0.015), zb = sz * (Z - lz);
      A.box([Math.min(xa, xb), y, Math.min(za, zb)], [Math.max(xa, xb), Math.min(y + 0.29, EAVE), Math.max(za, zb)], "stone_dressed");
    }
  }

  // ---- interior partition (kitchen | hall), both floors
  partition(A, { axis: "z", d0: -0.5, d1: -0.3, a0: -ZI, a1: ZI, y0: GF, y1: FF - 0.2,
    holes: [{ a0: 2.8, a1: 3.8, y0: GF - 1, y1: GF + 2.1 }, { a0: -3.0, a1: -2.0, y0: GF - 1, y1: GF + 2.1 }] });
  partition(A, { axis: "z", d0: -0.5, d1: -0.3, a0: -ZI, a1: ZI, y0: FF, y1: CEIL,
    holes: [{ a0: -3.0, a1: -2.0, y0: FF - 1, y1: FF + 2.1 }] });
  for (const [a0, a1, yy] of [[2.8, 3.8, GF], [-3.0, -2.0, GF], [-3.0, -2.0, FF]]) {
    A.box([-0.52, yy + 2.1, a0 - 0.1], [-0.28, yy + 2.26, a1 + 0.1], "oak_z");
    for (const z of [a0, a1]) A.box([-0.52, yy, z - (z === a0 ? 0.0 : 0.06)], [-0.28, yy + 2.1, z + (z === a0 ? 0.06 : 0.0)], "oak_y");
  }

  // ---- stairs: primary (hall, ascending toward the back) and rear service (east gable, ascending forward)
  const n = 15, run = 3.8;
  const s1 = stairs(A, { x0: -0.3, x1: 0.7, z0: 2.6, dir: -1, y0: GF, rise: FF - GF, n, run, openSide: +1 });
  const s2 = stairs(A, { x0: 4.5, x1: 5.5, z0: -3.0, dir: +1, y0: GF, rise: FF - GF, n, run, openSide: -1 });

  // ---- upper floor slab with two stairwells (boards over joists), main beam
  const slab = [
    [-XI, -0.3, -ZI, ZI], [-0.3, 0.7, -ZI, s1.holeZ[0]], [-0.3, 0.7, s1.holeZ[1], ZI],
    [0.7, 4.5, -ZI, ZI], [4.5, XI, s2.holeZ[1], ZI], [4.5, XI, -ZI, s2.holeZ[0]],
  ];
  for (const [xa, xb, za, zb] of slab) A.box([xa, FF - 0.2, za], [xb, FF, zb], "floor_x");
  for (let x = -XI + 0.35; x < XI - 0.2; x += 0.75) {
    for (const [xa, xb, za, zb] of slab) if (x - 0.06 >= xa && x + 0.06 <= xb) A.box([x - 0.06, FF - 0.36, za], [x + 0.06, FF - 0.2, zb], "oak_z", { k: 0 });
  }
  A.box([-XI, FF - 0.48, -1.75], [4.5, FF - 0.2, -1.45], "oak_x");
  // attic floor / upper ceiling with joists
  A.box([-XI, CEIL, -ZI], [XI, CEIL + 0.2, ZI], "floor_x");
  for (let x = -XI + 0.4; x < XI - 0.2; x += 0.8) A.box([x - 0.06, CEIL - 0.15, -ZI], [x + 0.06, CEIL, ZI], "oak_z", { k: 0 });

  // ---- stairwell balustrades on the upper floor
  railing(A, "z", s1.holeZ[0], s1.holeZ[1], 0.73, FF);
  railing(A, "x", -0.3, 0.7, s1.holeZ[1] + 0.03, FF);
  railing(A, "z", s2.holeZ[0], s2.holeZ[1], 4.47, FF);
  railing(A, "x", 4.5, XI, s2.holeZ[0] - 0.03, FF);

  // ---- hearth (west gable, kitchen) and chimney
  A.box([-XI, GF, -1.0], [-4.95, GF + 1.05, -0.7], "stone_dressed");
  A.box([-XI, GF, 0.7], [-4.95, GF + 1.05, 1.0], "stone_dressed");
  A.box([-XI, GF + 1.05, -1.15], [-4.85, GF + 1.3, 1.15], "oak_z");
  A.box([-XI, GF + 1.3, -0.95], [-5.05, FF - 0.2, 0.95], "plaster_smoke");
  A.box([-XI, GF, -0.7], [-XI + 0.05, GF + 1.05, 0.7], "soot");
  A.box([-XI, GF + 0.85, -0.7], [-4.95, GF + 1.05, 0.7], "soot", { k: 0 });
  A.box([-XI + 0.05, GF, -1.1], [-4.6, GF + 0.035, 1.1], "hearthstone", { k: 0 });
  A.box([-XI, GF, -1.0], [-4.95, GF + 1.05, 1.0], "stone", { v: 0 }); // keeps the player out of the firebox
  for (const z of [-0.35, 0.35]) { A.box([-5.25, GF + 0.035, z - 0.02], [-5.05, GF + 0.3, z + 0.02], "iron", { k: 0 }); }
  for (const [y, z, r] of [[GF + 0.16, -0.12, 8], [GF + 0.16, 0.12, -6], [GF + 0.27, 0.0, 3]]) A.obox([-5.2, y, z], [0.09, 0.09, 0.62], [0, 90 + r, 0], "bark", { k: 0 });
  A.box([-XI, FF, -0.6], [-5.1, CEIL, 0.6], "plaster");   // chimney breast upstairs
  A.box([-X, 7.6, -0.45], [-5.15, 10.0, 0.45], "stone");
  A.box([-X - 0.05, 10.0, -0.5], [-5.1, 10.12, 0.5], "stone_dressed");
  for (const z of [-0.2, 0.2]) A.box([-5.7, 10.12, z - 0.09], [-5.45, 10.4, z + 0.09], "ridge_tile");

  // ---- roof: slate on two planes, clay ridge, fascia + zinc gutters
  const th = Math.atan2(RIDGE - EAVE, Z), deg = (th * 180) / Math.PI, ov = 0.38, gov = 0.3, t = 0.14;
  for (const s of [1, -1]) {
    const zA = -0.12 * s, zB = s * (Z + ov);
    const yu = (z) => RIDGE - Math.abs(z) * Math.tan(th) * Math.sign(z * s || 1);
    const zm = (zA + zB) / 2, ym = (RIDGE + 0.12 * Math.tan(th) + (RIDGE - (Z + ov) * Math.tan(th))) / 2;
    const len = Math.abs(zB - zA) / Math.cos(th);
    const a = s > 0 ? deg : -deg;
    const nrm = [0, Math.cos((a * Math.PI) / 180), Math.sin((a * Math.PI) / 180)];
    A.obox([0, ym + nrm[1] * t / 2, zm + nrm[2] * t / 2], [2 * X + 2 * gov, t, len], [a, 0, 0], "slate");
    void yu;
    A.box([-X - gov, EAVE - ov * Math.tan(th) - 0.24, Math.min(s * (Z + ov - 0.03), s * (Z + ov))], [X + gov, EAVE - ov * Math.tan(th) + 0.02, Math.max(s * (Z + ov - 0.03), s * (Z + ov))], "oak_x");
    A.box([-X - gov, EAVE - ov * Math.tan(th) - 0.2, Math.min(s * (Z + ov), s * (Z + ov + 0.13))], [X + gov, EAVE - ov * Math.tan(th) - 0.1, Math.max(s * (Z + ov), s * (Z + ov + 0.13))], "zinc", { k: 0 });
  }
  A.obox([0, RIDGE + t / Math.cos(th) - 0.02, 0], [2 * X + 2 * gov + 0.04, 0.2, 0.2], [45, 0, 0], "ridge_tile");
  // barge boards
  for (const sx of [-1, 1]) for (const s of [1, -1]) {
    const a = s > 0 ? deg : -deg, len = (Z + ov) / Math.cos(th);
    const zm = s * (Z + ov) / 2, ym = (RIDGE + EAVE - ov * Math.tan(th)) / 2;
    A.obox([sx * (X + gov - 0.02), ym + 0.02, zm], [0.04, 0.24, len], [a, 0, 0], "oak_x", { k: 0 });
  }
  // downpipe
  A.box([X - 0.25, 0.05, Z + 0.06], [X - 0.17, EAVE - 0.3, Z + 0.14], "zinc", { k: 0 });

  // ---- furniture: kitchen
  table(A, "Kitchen table", -3.0, 0.6, 1.9, 0.85, GF);
  chair(A, "Kitchen chair", -3.5, 1.33, GF, "-z");
  chair(A, "Kitchen chair", -2.5, 1.33, GF, "-z");
  chair(A, "Kitchen chair", -1.65, 0.6, GF, "-x");
  bench(A, "Kitchen bench", -3.85, -2.15, -0.2, GF);
  dresser(A, "Dresser with plates", -3.3, -1.7, -ZI, +1, GF);
  chest(A, "Grain chest", -1.35, -0.55, -1.15, -0.5, GF, 0.7);
  // cast-iron range beside the hearth with its flue into the chimney
  A.beginFurniture("Cast-iron range");
  A.box([-XI, GF + 0.1, -2.55], [-4.85, GF + 0.8, -1.55], "cast_iron");
  A.box([-XI, GF + 0.8, -2.6], [-4.8, GF + 0.85, -1.5], "cast_iron");
  A.box([-4.86, GF + 0.25, -2.4], [-4.84, GF + 0.6, -2.05], "iron");
  A.box([-4.84, GF + 0.62, -2.45], [-4.81, GF + 0.645, -1.65], "brass");
  for (const z of [-2.5, -1.6]) A.box([-5.35, GF, z - 0.04], [-4.95, GF + 0.1, z + 0.04], "cast_iron");
  A.box([-5.3, GF + 0.85, -2.12], [-5.15, GF + 2.2, -1.97], "cast_iron");
  A.box([-XI, GF + 2.05, -2.12], [-5.15, GF + 2.2, -1.97], "cast_iron");
  A.box([-5.1, GF + 0.85, -2.3], [-4.9, GF + 0.98, -2.1], "copper");
  A.endFurniture();
  // copper pans on the wall, lamp on the table
  A.beginFurniture("Copper pans");
  for (const [z, s] of [[1.35, 0.22], [1.75, 0.28], [2.2, 0.32]]) A.box([-XI, GF + 1.35, z - s / 2], [-XI + 0.02, GF + 1.35 + s, z + s / 2], "copper");
  A.box([-XI, GF + 1.95, 1.15], [-XI + 0.05, GF + 1.98, 2.45], "iron");
  A.endFurniture(false);
  A.beginFurniture("Oil lamp");
  A.box([-3.08, GF + 0.76, 0.52], [-2.92, GF + 0.8, 0.68], "brass");
  A.box([-3.04, GF + 0.8, 0.56], [-2.96, GF + 0.9, 0.64], "brass");
  A.box([-3.03, GF + 0.9, 0.57], [-2.97, GF + 1.08, 0.63], "glass");
  A.endFurniture(false);
  // ---- hall / front room
  A.beginFurniture("Long-case clock");
  A.box([5.05, GF, 1.5], [XI, GF + 0.55, 1.98], "cherry_y");
  A.box([5.15, GF + 0.55, 1.58], [XI, GF + 1.75, 1.9], "cherry_y");
  A.box([5.0, GF + 1.75, 1.45], [XI, GF + 2.3, 2.03], "cherry_y");
  A.box([4.99, GF + 1.86, 1.56], [5.0, GF + 2.2, 1.92], "faience");
  A.box([5.13, GF + 0.8, 1.66], [5.15, GF + 1.6, 1.82], "glass");
  A.box([5.2, GF + 0.85, 1.71], [5.22, GF + 1.35, 1.77], "brass");
  A.endFurniture();
  A.beginFurniture("Armchair");
  A.box([3.0, GF + 0.38, 2.35], [3.7, GF + 0.48, 3.0], "wool_green");
  A.box([3.0, GF + 0.48, 2.85], [3.7, GF + 1.05, 3.0], "wool_green");
  for (const x of [3.0, 3.62]) A.box([x, GF + 0.48, 2.35], [x + 0.08, GF + 0.7, 3.0], "cherry_z");
  for (const x of [3.03, 3.62]) for (const z of [2.38, 2.92]) A.box([x, GF, z], [x + 0.05, GF + 0.38, z + 0.05], "cherry_y");
  A.endFurniture();
  table(A, "Side table", 4.15, 2.75, 0.5, 0.5, GF, 0.62);
  bench(A, "Hall bench", 1.35, 2.35, -3.82, GF);
  A.beginFurniture("Coat pegs");
  A.box([2.3, GF + 1.7, ZI - 0.03], [3.0, GF + 1.8, ZI], "oak_x");
  for (const x of [2.4, 2.65, 2.9]) A.box([x - 0.015, GF + 1.73, ZI - 0.12], [x + 0.015, GF + 1.76, ZI - 0.03], "oak_z");
  A.endFurniture(false);

  // ---- west bedroom
  bed(A, "Bed (west bedroom)", -XI + 0.02, -3.45, 1.3, 2.7, FF, "x0");
  armoire(A, "Armoire normande", -5.3, -3.9, -ZI, +1, FF);
  chest(A, "Blanket chest", -3.38, -2.95, 1.5, 2.5, FF, 0.5);
  A.beginFurniture("Washstand");
  A.box([-1.6, FF + 0.74, 3.35], [-0.85, FF + 0.78, ZI], "cherry_x");
  for (const x of [-1.56, -0.93]) for (const z of [3.39, 3.92]) A.box([x, FF, z], [x + 0.04, FF + 0.74, z + 0.04], "cherry_y");
  A.box([-1.38, FF + 0.78, 3.5], [-1.08, FF + 0.86, 3.8], "faience");
  A.box([-1.0, FF + 0.78, 3.7], [-0.9, FF + 1.0, 3.8], "faience_blue");
  A.endFurniture();
  chair(A, "Bedroom chair", -1.9, 0.0, FF, "-x");
  // ---- east bedroom / landing
  bed(A, "Bed (east bedroom)", 1.4, 2.8, 1.95, ZI - 0.02, FF, "z1");
  armoire(A, "Wardrobe", 3.0, 4.2, -ZI, +1, FF, 0.55, 2.1);
  A.beginFurniture("Writing desk");
  A.box([1.05, FF + 0.72, -ZI], [1.95, FF + 0.76, -3.45], "cherry_x");
  for (const x of [1.08, 1.88]) for (const z of [-3.97, -3.52]) A.box([x, FF, z], [x + 0.04, FF + 0.72, z + 0.04], "cherry_y");
  A.box([1.1, FF + 0.55, -3.95], [1.9, FF + 0.68, -3.5], "cherry_x");
  A.endFurniture();
  chair(A, "Desk chair", 1.5, -3.15, FF, "-z");

  // ---- scripted traversal (asset-local), run by --selftest through the real controller
  A.walkthrough = {
    start: { pos: [1.5, 0, 6.6], yaw: 0 },
    steps: [
      { goto: [1.5, 5.3], note: "approach front door" },
      { look: [1.5, 1.4, 4.0], hold: 0.5 }, { shot: "03_explore_front_door" },
      { interact: "front_door", aim: [1.6, 1.3, 4.03], expectOpen: true },
      { goto: [1.5, 3.55], expectY: GF, note: "stand in the door's swing" },
      { interact: "front_door", aim: [1.97, 1.3, 3.55], expectOpen: true, note: "closing door meets player and reopens" },
      { goto: [1.5, 3.2], expectY: GF, note: "enter hall" },
      { goto: [0.2, 3.3] }, { goto: [-0.9, 3.3], note: "kitchen doorway" },
      { goto: [-3.0, 3.05], expectY: GF, note: "kitchen window" },
      { look: [-3.0, 1.8, 4.6], hold: 0.4 }, { shot: "04a_kitchen_window_closed" },
      { interact: "kitchen_casement", aim: [-2.75, 1.9, 4.05], expectOpen: true },
      { interact: "kitchen_shutters", aim: [-3.0, 1.85, 4.52], expectOpen: true },
      { look: [-3.0, 1.5, 12.0], hold: 0.8 }, { shot: "04b_kitchen_window_open" },
      { goto: [-2.0, 2.3] }, { look: [-5.4, 0.9, -0.2], hold: 1.2 }, { shot: "05_furnished_kitchen" },
      { goto: [-0.9, 3.3] }, { goto: [0.2, 3.3] }, { goto: [0.2, 2.95], note: "front stair foot" },
      { goto: [0.2, 0.75], note: "halfway up the front stair" },
      { goto: [0.2, 1.9], note: "reverse direction mid-flight" },
      { goto: [0.2, -1.7], expectY: FF, note: "front staircase reaches upper floor" },
      { goto: [0.2, -2.4] }, { goto: [2.5, -1.9] }, { goto: [3.6, 1.3] }, { goto: [3.6, 3.35], expectY: FF, note: "east bedroom window" },
      { interact: "bedroom_e_front_casement", aim: [3.85, 4.8, 4.05], expectOpen: true },
      { look: [3.6, 2.6, 16.0], hold: 0.8 }, { shot: "06_upper_floor_view" },
      { goto: [3.6, 1.3] }, { goto: [5.0, 1.5], expectY: FF, note: "rear stair head" },
      { goto: [5.0, -3.45], expectY: GF, note: "rear service staircase reaches ground floor" },
      { goto: [3.9, -3.55] }, { goto: [3.35, -2.85], note: "clear of the rear door swing" },
      { interact: "rear_door", aim: [3.15, 1.3, -4.03], expectOpen: true },
      { goto: [3.2, -5.2], note: "exit through the rear door" },
      { interact: "rear_door", aim: [2.82, 1.3, -3.7], expectOpen: false, note: "close the rear door from outside" },
      { goto: [3.2, -6.4] }, { look: [3.0, 1.8, -4.5], hold: 0.6 }, { shot: "07_rear_exit" },
    ],
  };
  return A;
}


// =============================================================================
// Single-storey Norman stone cottage (chaumière en pierre), 8 x 6 m.
// Living room with hearth and a separate bedroom. One entrance, three shuttered windows.
// =============================================================================
function cottageBase() {
  const A = new Asset({
    id: "house_stone_cottage", version: 1, name: "Single-Story Stone Cottage", category: "Houses",
    description: "Compact rubble-stone cottage under slate. Living room with hearth, table and dresser; separate bedroom. One ledged door and three shuttered casements. Single storey: no stairs.",
    floors: 1, enterable: true, footprint: [8.2, 6.2], plinthDepth: 1.0, maxGrade: 0.8,
  });
  stdMaterials(A);
  const X = 4, Z = 3, T = 0.5, XI = X - T, ZI = Z - T, GF = 0.3, EAVE = 3.4, CEIL = 3.05, RIDGE = 5.6, y0 = GF - 0.02;
  A.box([-X - 0.05, -1.0, -Z - 0.05], [X + 0.05, y0, Z + 0.05], "stone_plinth");
  A.box([-XI, y0, -ZI], [XI, GF, ZI], "terracotta");
  const front = { axis: "x", outer: Z, inner: ZI }, back = { axis: "x", outer: -Z, inner: -ZI };
  const hf = [], hb = [];
  hf.push(door(A, { id: "front_door", ...front, c: 0.6, floor: GF, w: 1.0, h: 2.05, hingeSide: -1 }));
  hf.push(window(A, { id: "living", ...front, c: -2.2, sill: GF + 0.9, w: 1.0, h: 1.25 }));
  hf.push(window(A, { id: "bedroom", ...front, c: 2.4, sill: GF + 0.9, w: 0.9, h: 1.2, shuttersOpen: false }));
  hb.push(window(A, { id: "living_rear", ...back, c: -1.5, sill: GF + 0.9, w: 0.9, h: 1.2 }));
  const pn = (side) => ({ side, family: "masonry", bands: [[y0, EAVE, 0, GF]] });
  wall(A, { ...front, aOut: [-X, X], aIn: [-XI, XI], y0, y1: EAVE, holes: hf, panel: pn("front") });
  wall(A, { ...back, aOut: [-X, X], aIn: [-XI, XI], y0, y1: EAVE, holes: hb, panel: pn("rear") });
  wall(A, { axis: "z", outer: X, inner: XI, aOut: [-ZI - 0.03, ZI + 0.03], aIn: [-ZI, ZI], y0, y1: EAVE, holes: [], panel: pn("east") });
  wall(A, { axis: "z", outer: -X, inner: -XI, aOut: [-ZI - 0.03, ZI + 0.03], aIn: [-ZI, ZI], y0, y1: EAVE, holes: [], panel: pn("west") });
  quoins(A, X, Z, y0, EAVE);
  partition(A, { axis: "z", d0: 1.2, d1: 1.35, a0: -ZI, a1: ZI, y0: GF, y1: CEIL, holes: [{ a0: -2.2, a1: -1.3, y0: GF - 1, y1: GF + 2.05 }] });
  A.box([1.18, GF + 2.05, -2.3], [1.37, GF + 2.2, -1.2], "oak_z");
  A.box([-XI, CEIL, -ZI], [XI, CEIL + 0.2, ZI], "floor_x");
  for (let x = -XI + 0.4; x < XI - 0.2; x += 0.8) A.box([x - 0.06, CEIL - 0.15, -ZI], [x + 0.06, CEIL, ZI], "oak_z", { k: 0 });
  A.box([-XI, CEIL - 0.42, -0.15], [XI, CEIL - 0.15, 0.15], "oak_x");
  gableRoof(A, { X, Z, XI, EAVE, RIDGE });
  // hearth on the west gable + chimney stack
  A.box([-XI, GF, -0.85], [-3.0, GF + 1.0, -0.6], "stone_dressed");
  A.box([-XI, GF, 0.6], [-3.0, GF + 1.0, 0.85], "stone_dressed");
  A.box([-XI, GF + 1.0, -1.0], [-2.9, GF + 1.22, 1.0], "oak_z");
  A.box([-XI, GF + 1.22, -0.8], [-3.08, CEIL, 0.8], "plaster_smoke");
  A.box([-XI, GF, -0.6], [-XI + 0.05, GF + 1.0, 0.6], "soot");
  A.box([-XI + 0.05, GF, -0.95], [-2.7, GF + 0.035, 0.95], "hearthstone", { k: 0 });
  A.box([-XI, GF, -0.85], [-3.0, GF + 1.0, 0.85], "stone", { v: 0 });
  for (const [y, z, r] of [[GF + 0.15, -0.1, 6], [GF + 0.15, 0.12, -5]]) A.obox([-3.25, y, z], [0.09, 0.09, 0.55], [0, 90 + r, 0], "bark", { k: 0 });
  A.box([-X, 4.6, -0.4], [-3.2, 6.3, 0.4], "stone");
  A.box([-X - 0.05, 6.3, -0.45], [-3.15, 6.42, 0.45], "stone_dressed");
  // furniture
  table(A, "Table", -1.6, 0.1, 1.4, 0.8, GF);
  chair(A, "Chair", -2.0, 0.8, GF, "-z");
  chair(A, "Chair", -1.2, -0.6, GF, "+z");
  dresser(A, "Dresser with plates", -0.9, 0.4, -ZI, +1, GF);
  bed(A, "Bed", 2.15, 3.45, -1.05, 0.95, GF, "z0");
  chest(A, "Blanket chest", 2.6, 3.4, 1.1, 1.45, GF, 0.5);
  chair(A, "Bedroom chair", 3.1, -2.05, GF, "-x");
  A.beginFurniture("Copper pans");
  for (const [z, s] of [[1.05, 0.22], [1.45, 0.28]]) A.box([-XI, GF + 1.3, z - s / 2], [-XI + 0.02, GF + 1.3 + s, z + s / 2], "copper");
  A.endFurniture(false);
  A.walkthrough = {
    start: { pos: [0.6, 0, 5.0], yaw: 0 },
    steps: [
      { goto: [0.6, 3.8], note: "approach cottage door" },
      { interact: "front_door", aim: [0.8, 1.3, 2.53], expectOpen: true },
      { goto: [0.6, 1.6], expectY: GF, note: "enter cottage" },
      { goto: [-2.2, 1.85], expectY: GF, note: "living-room window" },
      { interact: "living_casement", aim: [-2.0, 1.9, 2.55], expectOpen: true },
      { interact: "living_casement", aim: [-2.67, 1.9, 2.3], expectOpen: false, note: "close it again" },
      { goto: [-0.4, 1.6] }, { goto: [0.5, -1.6] }, { goto: [1.75, -1.75], expectY: GF, note: "bedroom doorway" },
      { goto: [1.75, 1.6] }, { goto: [2.25, 1.75], expectY: GF, note: "bedroom window" },
      { interact: "bedroom_casement", aim: [2.6, 1.85, 2.55], expectOpen: true },
      { interact: "bedroom_shutters", aim: [2.4, 1.8, 3.02], expectOpen: true },
      { look: [2.4, 1.6, 9.0], hold: 0.6 }, { shot: "08_cottage_bedroom_window" },
      { goto: [1.75, 1.6] }, { goto: [1.75, -1.75] }, { goto: [0.5, -1.6] }, { goto: [0.6, 1.6] }, { goto: [0.6, 4.2], note: "leave cottage" },
    ],
  };
  return A;
}

// ============================================================================ the buildings
function farmhouse2() {
  const A = farmhouseBase();
  // the old cap slab and pots on the west stack give way to a kit cap
  drop(A, (p) => p.t === "b" && p.c[0] < -5 && p.c[1] > 10.0 && (p.m === SD || p.m === "ridge_tile"));
  chimneyCap(A, { x: -5.575, z: 0, y: 10.16, w: 0.85, d: 0.9 });
  const S = { X: 6, Z: 4.5, T: 0.5, GF: 0.3, EAVE: 6.3, RIDGE: 8.8, floors: [3.3], stringDrop: 0.38, yard: "farm", gardenWall: 7.5,
    timber: { "+z": { above: 3.1, rails: [4.18] }, "-z": { above: 3.1, rails: [4.18] } },
    dormers: [[-3.0, 1, 1.0, 0.95], [3.6, 1, 1.0, 0.95]], chimneys: [{ x: 4.6, m: "stone" }],
    ivy: [{ face: "+x", a0: -3.6, a1: 3.6, y0: 0.3, y1: 5.6, density: 0.85 }, { face: "-z", a0: -6.0, a1: -3.8, y0: 0.3, y1: 3.0, density: 0.7 }],
    note: "B2: half-timbered upper storey (Norman colombage) over a rubble ground floor with a string course, chamfered plinth, bevelled quoins and dressings, keystoned openings, two dormers, capped chimneys, ivy on the east gable." };
  return dress(A, S);
}
function cottage2() {
  const A = cottageBase();
  drop(A, (p) => p.t === "b" && p.c[0] < -3 && p.c[1] > 6.29 && p.m === SD);
  chimneyCap(A, { x: -3.6, z: 0, y: 6.46, w: 0.8, d: 0.8 });
  return dress(A, { X: 4, Z: 3, T: 0.5, GF: 0.3, EAVE: 3.4, RIDGE: 5.6, yard: "cottage", dormers: [[-2.2, 1, 0.9, 0.8]],
    ivy: [{ face: "-x", a0: -2.5, a1: 2.5, y0: 0.3, y1: 3.2, density: 0.9 }, { face: "+z", a0: -4.0, a1: -2.9, y0: 0.3, y1: 3.0, density: 0.7 }],
    note: "B2: chamfered plinth, bevelled quoins, keystoned openings, a stepped cornice, a dormer in the slate roof, a capped stack and ivy on the hearth gable." });
}
function workshop2() {
  return dress(workshop(), { X: 4.5, Z: 3, T: 0.45, GF: 0.15, EAVE: 3.3, RIDGE: 5.3, yard: "work", cart: true, woodpile: true, chimneys: [{ x: -3.6, z: -1.2, y0: 3.8, y1: 6.2, w: 0.55, d: 0.55 }],
    ivy: [{ face: "+x", a0: -2.5, a1: 2.5, y0: 0.15, y1: 3.0, density: 0.8 }], note: "B2: a dressed cart arch over the double doors, chamfered plinth, bevelled quoins and dressings, keystones, a capped stove flue, ivy on the east gable." });
}
function dairy2() {
  const A = dairy();
  remat(A, { min: [-3.1, 0, -2.6], max: [3.1, 0, 2.6], above: -9 });   // limewashed (laiterie)
  return dress(A, { X: 3, Z: 2.5, T: 0.5, GF: 0.15, EAVE: 2.9, RIDGE: 4.5, yard: "farm", well: true,
    ivy: [{ face: "-x", a0: -2.0, a1: 2.0, y0: 0.15, y1: 2.6, density: 0.8 }], note: "B2: limewashed walls over dressed quoins and a chamfered plinth, keystoned openings, ivy on the west gable." });
}
function storehouse2() {
  return dress(storehouse(), { X: 3.5, Z: 2.5, T: 0.45, GF: 0.15, EAVE: 3.0, RIDGE: 4.8, yard: "farm", well: false, cornice: [],
    ivy: [{ face: "-x", a0: -2.0, a1: 2.0, y0: 0.15, y1: 2.8, density: 0.85 }, { face: "-z", a0: -3.5, a1: 0.5, y0: 0.15, y1: 2.4, density: 0.6 }],
    note: "B2: a cart arch over the doors, chamfered plinth, bevelled quoins and dressings, ivy on the west gable and rear." });
}
function office2() {
  return dress(office(), { X: 2.5, Z: 2.0, T: 0.35, GF: 0.2, EAVE: 3.0, RIDGE: 4.2, yard: "town", sandbags: true, chimneys: [{ x: -1.85, z: -0.4, y0: 3.4, y1: 5.0, w: 0.45, d: 0.45, m: "brick" }],
    extra: (A, { Fs }) => { for (const f of ["+z", "-z"]) for (const a of [-2.29, 2.29]) pilaster(A, Fs[f], a, 0.2, 2.98, { w: 0.4, proj: 0.06 }); },
    ivy: [{ face: "+x", a0: -1.6, a1: 1.6, y0: 0.2, y1: 2.6, density: 0.7 }], note: "B2: rendered walls framed by corner pilasters, a chamfered plinth, dressed surrounds, a capped stove chimney, ivy on the east wall." });
}
// ---- large town buildings (catalog_large.mjs): ridge along X, floors h apart from GF 0.3
function school2() {
  return dress(schoolhouse(), { X: 10, Z: 4, T: 0.5, GF: 0.3, EAVE: 10.5, RIDGE: 13.7, floors: [3.7, 7.1], yard: "town", fence: true, beds: true,
    dormers: [[-5.2, 1], [0, 1], [5.2, 1]], chimneys: [{ x: -7.4, m: "brick" }, { x: 7.4, m: "brick" }],
    extra: (A) => turret(A, { x: 0, z: 0, y0: 13.0, y1: 14.9, r: 0.55, spire: 1.9, sides: 8, m: SD, band: SD }),
    ivy: [{ face: "-x", a0: -3.4, a1: 3.4, y0: 0.3, y1: 7.0, density: 0.85 }, { face: "-z", a0: 8.0, a1: 10.0, y0: 0.3, y1: 5.0, density: 0.7 }],
    note: "B2: brick with stone dressings: chamfered plinth, string courses at each floor, bevelled quoins, keystoned openings, a stepped cornice, three dormers, an octagonal bell turret on the ridge, capped chimneys, ivy on the west gable." });
}
function inn2() {
  const yAt = (f) => 0.3 + f * 3.2;
  return dress(inn(), { X: 7.5, Z: 4, T: 0.5, GF: 0.3, EAVE: 13.1, RIDGE: 16.3, floors: [3.5, 6.7, 9.9], yard: "town", beds: false, cart: true,
    timber: { "+z": { above: yAt(2) - 0.2, rails: [yAt(3) - 0.2, yAt(2) + 0.85, yAt(3) + 0.85], proj: 0.03, render: false }, "-z": { above: yAt(2) - 0.2, rails: [yAt(3) - 0.2], proj: 0.03, render: false } },
    dormers: [[-3.5, 1], [3.5, 1], [0, -1]], chimneys: [{ x: -5.6 }, { x: 5.6 }],
    extra: (A, { Fs }) => {
      // the arcade: six round columns carrying a terrace at the first floor (walked onto from its windows)
      const d = 4 + 1.3, top = yAt(1) - 0.14;
      for (let i = 0; i < 6; i++) A.box([-6.25 + 2.5 * i - 0.3, -0.8, d - 0.3], [-6.25 + 2.5 * i + 0.3, 0.02, d + 0.3], "stone_plinth");
      colonnade(A, { axis: "x", a0: -6.25, a1: 6.25, d, y0: 0, y1: top, n: 6, r: 0.2, depth: 0.5 });
      balcony(A, Fs["+z"], { a0: -6.6, a1: 6.6, y: yAt(1), depth: 1.62, t: 0.14 });
      balcony(A, Fs["+z"], { a0: -0.8, a1: 0.8, y: yAt(2), depth: 0.75 });
    },
    ivy: [{ face: "+x", a0: -3.0, a1: 3.0, y0: 0.3, y1: 6.0, density: 0.8 }],
    note: "B2: a round-arched arcade on six columns carries a first-floor terrace; half-timbered upper storeys with a balcony, string courses, chamfered plinth, keystoned surrounds, dormers, capped chimneys, ivy on the east gable." });
}
function warehouse2() {
  return dress(warehouse(), { X: 9, Z: 4.5, T: 0.5, GF: 0.3, EAVE: 11.7, RIDGE: 14.9, floors: [4.1, 7.9], yard: "work", cart: true, sandbags: true, xobst: true,
    dormers: [[0, 1, 1.6, 1.4]],
    extra: (A, { Fs }) => {
      for (const f of ["+z", "-z"]) for (const a of f === "+z" ? [-7.2, -4.3, -1.95, 1.95, 4.3, 7.2] : [-7.6, -3.6, 0.2, 3.9, 7.6]) pilaster(A, Fs[f], a, 0.3, 11.2, { w: 0.42, proj: 0.07 });
      A.box([-0.15, 13.6, 3.2], [0.15, 13.9, 5.3], "oak_z", { k: 0 });   // hoist beam out of the loading dormer
      A.cyl([0, 13.45, 5.15], [0.02, 0.3, 0.02], "iron", { n: 4, k: 0 });
    },
    ivy: [{ face: "-x", a0: -4.0, a1: 4.0, y0: 0.3, y1: 7.5, density: 0.85 }, { face: "-z", a0: -9.0, a1: -7.0, y0: 0.3, y1: 5.0, density: 0.7 }],
    note: "B2: a dressed cart arch over the double doors, pilaster strips between the bays, string courses, chamfered plinth, a stepped cornice, a loading dormer with its hoist beam, ivy on the west gable." });
}
function municipal2() {
  const yAt = (f) => 0.3 + f * 3.3;
  return dress(municipal(), { X: 8, Z: 4.5, T: 0.5, GF: 0.3, EAVE: 16.8, RIDGE: 20.2, floors: [3.6, 6.9, 10.2, 13.5], yard: "town", sandbags: true,
    dormers: [[-4.5, 1], [4.5, 1], [-4.5, -1], [4.5, -1]], chimneys: [{ x: -6.4 }, { x: 6.4 }],
    extra: (A, { Fs }) => {
      for (const f of ["+z", "-z"]) for (const a of f === "+z" ? [-5.6, -2.2, 2.2, 5.6] : [-4.0, 0, 4.0]) pilaster(A, Fs[f], a, 0.3, 16.3, { w: 0.46, proj: 0.08 });
      // portico: four columns in front of the door carrying the council chamber's balcony
      const d = 4.5 + 1.4, top = yAt(1) - 0.14;
      for (let i = 0; i < 4; i++) A.box([-2.4 + 1.6 * i - 0.3, -0.8, d - 0.3], [-2.4 + 1.6 * i + 0.3, 0.02, d + 0.3], "stone_plinth");
      colonnade(A, { axis: "x", a0: -2.4, a1: 2.4, d, y0: 0, y1: top, n: 4, r: 0.21, depth: 0.5 });
      balcony(A, Fs["+z"], { a0: -2.75, a1: 2.75, y: yAt(1), depth: 1.72, t: 0.14 });
      for (const f of [2, 3]) for (const a of [-3.4, 3.4]) balcony(A, Fs["+z"], { a0: a - 0.75, a1: a + 0.75, y: yAt(f), depth: 0.7 });
    },
    ivy: [{ face: "+x", a0: -3.0, a1: 3.0, y0: 0.3, y1: 6.5, density: 0.75 }],
    note: "B2: a four-column arcaded portico carrying the council chamber balcony, giant pilasters, iron balconies on the second and third floors, string courses, chamfered plinth, keystoned surrounds, a stepped cornice, dormers, capped chimneys." });
}
// ---- village2 (catalog_village2.mjs)
function longere2() {
  const A = normanFarmhouse();
  chimneyCap(A, { x: -7.075, z: 0, y: 8.86, w: 0.95, d: 1.2 });
  return dress(A, { X: 7.5, Z: 3.5, T: 0.6, GF: 0.3, EAVE: 3.4, RIDGE: 7.6, yard: "farm", gardenWall: 7.0,
    timber: { "+z": { above: 0.28, rails: [1.15], sp: 0.7 } }, dormers: [[-2.4, 1, 0.95, 0.95], [3.4, 1, 0.95, 0.95]],
    ivy: [{ face: "+x", a0: -2.5, a1: 2.5, y0: 0.3, y1: 3.2, density: 0.85 }, { face: "-z", a0: 2.0, a1: 7.5, y0: 0.3, y1: 2.8, density: 0.7 }],
    note: "B2: Pays d'Auge colombage on the front (oak posts, rails and braces over rendered infill), dressed rubble rear and gables, chamfered plinth, two dormers in the steep slate roof, a capped stack, ivy on the cellier gable." });
}
function barn2() {
  return dress(stoneBarn(), { X: 8, Z: 4.5, T: 0.6, GF: 0.15, EAVE: 5.0, RIDGE: 9.0, yard: "farm", well: false, cornice: [],
    extra: (A, { Fs }) => { for (const a of [-2.5, 1.5, 5.6]) { fbox(A, Fs["-z"], a - 0.32, a + 0.32, -0.3, 3.0, -0.01, 0.62, "stone", { bv: 0.02 }); fpart(A, "wedge", Fs["-z"], a, 3.25, 0.3, [0.64, 0.5, 0.62], SD, { top: 0.08 }); } },
    ivy: [{ face: "-x", a0: -3.5, a1: 3.5, y0: 0.15, y1: 4.5, density: 0.9 }, { face: "-z", a0: -8.0, a1: -4.8, y0: 0.15, y1: 3.5, density: 0.7 }],
    more: (add) => { add("drs_hay_bale_stack", 3.5, 7.5, 0, 0.7, 0.6); add("drs_woodpile", 8 + 1.35, 2.0, 90, 0.6, 0.2); },
    note: "B2: a dressed cart arch over the cart doors, three buttresses with sloped set-offs on the rear wall, chamfered plinth, bevelled quoins, ivy on the west gable and rear." });
}

// ============================================================================ church (moved from catalog_village2.mjs)
// Tower (west, x -14.4..-8.0) with a four-flight stair to the belfry at 10.7 m (round-arched openings on three sides:
// the sniper position) under an octagonal spire with corner pinnacles to 25 m; nave (x -8..6, 8 m wide, 6 m to the eaves) with pews;
// choir (x 6..11). B2: round-arched windows and belfry openings (the wall holes run up into the arch, the spandrels close
// them), a two-order portal on colonnettes, bevelled buttresses with sloped set-offs, plinth, drip course and cornice.
function church2() {
  const A = new Asset({ id: "vil_church", version: 2, name: "Village church with steeple", category: "Landmarks",
    description: "Norman village church (Saint-Laurent): a 14 x 8 m nave with pews, a narrower choir with the altar, and a west tower 6.4 m square rising 15 m to the belfry under an octagonal slate spire with four corner pinnacles (25 m). A four-flight stair climbs the tower to the belfry floor at 10.7 m, whose open round-arched openings on three sides are the sniper position. South door into the nave through a two-order arched portal, south door into the tower base. Round-arched windows with keystones and fanlights, buttresses with sloped set-offs, a drip course and cornice, ivy on the tower.",
    floors: 1, enterable: true, footprint: [28.8, 9.4], plinthDepth: 1.0, maxGrade: 0.8 });
  stdMaterials(A);
  A.mat("bronze", [0.3, 0.22, 0.1], 0.45, "metal", { metallic: 0.9 });
  const GF = 0.3, y0 = GF - 0.02, NT = 0.8, arches = [];
  // a round-headed opening: the wall hole runs up `rise` into the head; the arch is dressed after the walls are built
  const head = (hole, F, a0, a1, top, o = {}) => { const rise = (a1 - a0) / 2; arches.push({ F, a0, a1, top, rise, ...o }); return { ...hole, y1: top + rise + 0.02 }; };
  // ---- tower: outer x -14.4..-8.0, z -3.2..3.2; inner x -13.6..-8.8, z -2.4..2.4
  const TX0 = -14.4, TX1 = -8.0, TZ = 3.2, TT = 0.8, TXI0 = TX0 + TT, TXI1 = TX1 - TT, TZI = TZ - TT, TOP = 15.0, BF = 10.7;
  const FtS = { axis: "x", d: TZ, out: 1 }, FtN = { axis: "x", d: -TZ, out: -1 }, FtW = { axis: "z", d: TX0, out: -1 };
  const tdoor = door(A, { id: "tower_door", axis: "x", outer: TZ, inner: TZI, c: -11.2, floor: GF, w: 1.1, h: 2.3, hingeSide: 1 });
  const belfry = (F, c) => head({ a0: c - 0.55, a1: c + 0.55, y0: BF + 0.9, y1: BF + 3.2 }, F, c - 0.55, c + 0.55, BF + 3.2, { T: TT, w: 0.22 });
  const lancet = (F, c, y) => head({ a0: c - 0.15, a1: c + 0.15, y0: y, y1: y + 1.1 }, F, c - 0.15, c + 0.15, y + 1.1, { T: TT, w: 0.12, key: false });
  const tArch = { a0: -0.7, a1: 0.7, y0: -10, y1: GF + 2.7 };
  const tp = (s) => pnM(s, y0, BF - 0.4, GF);
  A.box([TX0 - 0.05, -0.9, -TZ - 0.05], [TX1, y0, TZ + 0.05], "stone_plinth");
  A.box([TXI0, y0, -TZI], [TXI1, GF, TZI], SD);
  wall(A, { axis: "x", outer: TZ, inner: TZI, aOut: [TX0, TX1], aIn: [TXI0, TXI1], y0, y1: TOP, holes: [tdoor, belfry(FtS, -11.2), lancet(FtS, -11.2, 6.2)], pt: PT, panel: tp("tower_s") });
  wall(A, { axis: "x", outer: -TZ, inner: -TZI, aOut: [TX0, TX1], aIn: [TXI0, TXI1], y0, y1: TOP, holes: [belfry(FtN, -11.2), lancet(FtN, -11.2, 4.0)], pt: PT, panel: tp("tower_n") });
  wall(A, { axis: "z", outer: TX0, inner: TXI0, aOut: [-TZI - PT, TZI + PT], aIn: [-TZI, TZI], y0, y1: TOP, holes: [belfry(FtW, 0)], pt: PT, panel: tp("tower_w") });
  wall(A, { axis: "z", outer: TX1, inner: TXI1, aOut: [-TZI - PT, TZI + PT], aIn: [-TZI, TZI], y0, y1: TOP, holes: [tArch], pt: PT, panel: null });
  // clasping buttresses at the tower corners with sloped set-offs
  for (const s of [-1, 1]) for (const x of [TX0, TX1 - 0.6]) {
    const top = 7.0 - (x === TX0 ? 0 : 1.5), x0 = x - (x === TX0 ? 0.25 : 0), x1 = x + 0.6 + (x === TX0 ? 0 : 0.25), z0 = s > 0 ? TZ - 0.6 : -TZ - 0.25, z1 = s > 0 ? TZ + 0.25 : -TZ + 0.6;
    A.box([x0, y0, z0], [x1, top, z1], SD, { bv: 0.02 });
    A.wedge([(x0 + x1) / 2, top + 0.2, s > 0 ? TZ + 0.0 : -TZ - 0.0], [x1 - x0, 0.4, 0.5], SD, { top: 0.1, r: [0, s > 0 ? 0 : 180, 0] });
  }
  A.box([TX0 - 0.15, TOP - 0.3, -TZ - 0.15], [TX1 + 0.15, TOP, TZ + 0.15], SD, { bv: 0.03 });   // cornice
  A.box([TX0 - 0.08, BF - 0.36, -TZ - 0.08], [TX1 + 0.08, BF - 0.2, TZ + 0.08], SD, { bv: 0.015, k: 0 });   // belfry string course
  for (const [axis, d0, d1, c] of [["x", TZI, TZ, -11.2], ["x", -TZ, -TZI, -11.2], ["z", TX0, TXI0, 0]]) for (let k = 0; k < 2; k++) wbox(A, axis, c - 0.55, c + 0.55, BF + 2.75 + k * 0.22, BF + 2.82 + k * 0.22, d0, d1, "oak_x", { k: 0 });   // abat-son slats (top only)
  // octagonal broach spire on the tower top, slate pinnacles at the corners, the iron cross
  const scx = (TX0 + TX1) / 2, SW = 2 * TZ;
  A.cone([scx, TOP - 0.02 + 5.0, 0], [SW, 10.0, SW], "slate", { n: 8 });
  for (const sx of [-1, 1]) for (const sz of [-1, 1]) A.cone([scx + sx * (TZ - 0.5), TOP - 0.02 + 1.1, sz * (TZ - 0.5)], [1.3, 2.2, 1.3], "slate", { n: 4, r: [0, 45, 0] });
  A.cyl([scx, 25.0 - 0.6, 0], [0.18, 0.5, 0.18], "slate", { n: 8, k: 0 });
  A.box([scx - 0.05, 25.0 - 0.2, -0.05], [scx + 0.05, 25.0 + 1.4, 0.05], "iron", { k: 0 });
  A.box([scx - 0.35, 25.0 + 0.85, -0.04], [scx + 0.35, 25.0 + 0.95, 0.04], "iron", { k: 0 });
  // tower stair: W strip x -13.6..-12.6 climbs +Z, E strip x -9.8..-8.8 climbs -Z, landings at the ends
  const fl = [[TXI0, TXI0 + 1.0, -1.6, 1, GF], [TXI1 - 1.0, TXI1, 1.6, -1, GF + 2.6], [TXI0, TXI0 + 1.0, -1.6, 1, GF + 5.2], [TXI1 - 1.0, TXI1, 1.6, -1, GF + 7.8]];
  fl.forEach(([x0, x1, z0, dir, yb]) => {
    const west = dir > 0;
    stairs(A, { x0, x1, z0, dir, y0: yb, rise: 2.6, n: 12, run: 3.2, openSide: west ? 1 : -1, core: !west });
    if (!west) {   // E flights: no spandrel (the ground passage runs under them); a vertical guard per tread toward the void
      for (let k = 0; k < 12; k++) { const za = z0 - k * (3.2 / 12), zb = za - 3.2 / 12, top = yb + (k + 1) * (2.6 / 12); A.box([x0 - 0.06, top - 0.05, zb], [x0, top + 1.1, za], "oak_x", { v: 0 }); }
      A.obox([x0 - 0.03, yb + 1.3 + 0.9, 0], [0.05, 0.07, Math.hypot(3.2, 2.6)], [Math.atan2(2.6, 3.2) * 180 / Math.PI, 0, 0], "oak_x", { k: 0 });
    }
  });
  for (const [z0, z1, y] of [[1.6, TZI, GF + 2.6], [-TZI, -1.6, GF + 5.2], [1.6, TZI, GF + 7.8]]) {
    A.box([TXI0, y - 0.2, z0], [TXI1, y, z1], "floor_x");
    railing(A, "x", TXI0 + 1.0, TXI1 - 1.0, z0 > 0 ? z0 - 0.03 : z1 + 0.03, y);
  }
  // belfry floor with the stair hole over the last E flight, its guard, the bell frame and bell
  A.box([TXI0, BF - 0.2, -TZI], [TXI1 - 1.0, BF, TZI], "floor_x");
  A.box([TXI1 - 1.0, BF - 0.2, -TZI], [TXI1, BF, -1.6], "floor_x");
  railing(A, "z", -1.6, TZI, TXI1 - 1.03, BF);
  for (const x of [-12.2, -10.6]) for (const z of [-0.6, 1.0]) A.box([x - 0.1, BF, z - 0.1], [x + 0.1, BF + 3.6, z + 0.1], "oak_y");
  for (const z of [-0.6, 1.0]) A.box([-12.3, BF + 3.4, z - 0.12], [-10.5, BF + 3.6, z + 0.12], "oak_x", { k: 0 });
  A.box([-11.6, BF + 3.6, -0.7], [-11.2, BF + 3.75, 1.1], "oak_z", { k: 0 });
  for (const [w, y] of [[0.5, 3.35], [0.75, 3.0], [0.95, 2.6]]) A.box([-11.4 - w / 2, BF + y, 0.2 - w / 2], [-11.4 + w / 2, BF + y + 0.38, 0.2 + w / 2], "bronze", { k: 0 });
  A.box([TXI0, BF + 3.8, -TZI], [TXI1, BF + 4.0, TZI], "floor_x");   // belfry ceiling under the spire
  // ---- nave: outer x -8.0..6.0, z -4..4; inner x -7.2..5.2, z -3.2..3.2
  const NX0 = -8.0, NX1 = 6.0, NZ = 4.0, NXI0 = NX0 + NT, NXI1 = NX1 - NT, NZI = NZ - NT, NE = 6.0, NR = 10.0;
  const FnS = { axis: "x", d: NZ, out: 1 }, FnN = { axis: "x", d: -NZ, out: -1 };
  const sdoor = door(A, { id: "south_door", axis: "x", outer: NZ, inner: NZI, c: -2.6, floor: GF, w: 1.6, h: 2.8, double: true, label: "church doors" });
  const cw = (id, F, outer, inner, c, sill = GF + 2.4, h = 2.2) => head(window(A, { id, axis: F.axis, outer, inner, c, sill, w: 0.9, h, shutters: false }), F, c - 0.45, c + 0.45, sill + h, { T: NT, glass: true, id });
  const nS = [sdoor, cw("nave_s1", FnS, NZ, NZI, -5.8), cw("nave_s2", FnS, NZ, NZI, 1.0), cw("nave_s3", FnS, NZ, NZI, 3.8)];
  const nN = [cw("nave_n1", FnN, -NZ, -NZI, -5.8), cw("nave_n2", FnN, -NZ, -NZI, -2.6), cw("nave_n3", FnN, -NZ, -NZI, 1.0), cw("nave_n4", FnN, -NZ, -NZI, 3.8)];
  const chancel = { a0: -1.8, a1: 1.8, y0: -10, y1: GF + 4.4 };
  A.box([NX0, -0.9, -NZ - 0.05], [NX1, y0, NZ + 0.05], "stone_plinth");
  A.box([NXI0, y0, -NZI], [NXI1, GF, NZI], SD);
  const np = (s) => pnM(s, y0, NE, GF);
  wall(A, { axis: "x", outer: NZ, inner: NZI, aOut: [NX0, NX1], aIn: [NXI0, NXI1], y0, y1: NE, holes: nS, pt: PT, panel: np("nave_s") });
  wall(A, { axis: "x", outer: -NZ, inner: -NZI, aOut: [NX0, NX1], aIn: [NXI0, NXI1], y0, y1: NE, holes: nN, pt: PT, panel: np("nave_n") });
  wall(A, { axis: "z", outer: NX0, inner: NXI0, aOut: [-NZI - PT, NZI + PT], aIn: [-NZI, NZI], y0, y1: NE, holes: [tArch], pt: PT, panel: null });
  wall(A, { axis: "z", outer: NX1, inner: NXI1, aOut: [-NZI - PT, NZI + PT], aIn: [-NZI, NZI], y0, y1: NE, holes: [chancel], pt: PT, panel: null });
  for (const s of [-1, 1]) for (const x of [-4.3, -0.8, 2.6]) {   // buttresses with a set-off and a sloped top
    const z0 = s > 0 ? NZ - 0.01 : -NZ - 0.55, z1 = s > 0 ? NZ + 0.55 : -NZ + 0.01;
    A.box([x - 0.35, y0, z0], [x + 0.35, 2.4, z1], SD, { bv: 0.02 });
    A.box([x - 0.3, 2.39, s > 0 ? z0 : z0 + 0.15], [x + 0.3, NE - 1.2, s > 0 ? z1 - 0.15 : z1], SD, { bv: 0.02 });
    A.wedge([x, NE - 1.2 + 0.25, s * (NZ + 0.2)], [0.6, 0.5, 0.4], SD, { top: 0.1, r: [0, s > 0 ? 0 : 180, 0] });
  }
  roofX(A, NX0, NX1, NZ, NE, NR, { gables: [false, true], wallT: NT });
  A.prism(0, NX0, NX0 + NT, [[-NZ, NE], [NZ, NE], [0, NR]], "stone");
  // ---- choir: outer x 6..11, z -3..3; inner x 6..10.2, z -2.2..2.2 (its windows sit lower than the nave's, under the choir eaves)
  const CX1 = 11.0, CZ = 3.0, CXI1 = CX1 - NT, CZI = CZ - NT, CE = 5.0, CR = 7.8;
  const FcS = { axis: "x", d: CZ, out: 1 }, FcN = { axis: "x", d: -CZ, out: -1 }, FcE = { axis: "z", d: CX1, out: 1 };
  A.box([NX1, -0.9, -CZ - 0.05], [CX1 + 0.05, y0, CZ + 0.05], "stone_plinth");
  A.box([NX1 - 0.02, y0, -CZI], [CXI1, GF + 0.15, CZI], SD);   // choir floor one step up
  const cp = (s) => pnM(s, y0, CE, GF);
  wall(A, { axis: "x", outer: CZ, inner: CZI, aOut: [NX1, CX1], aIn: [NX1, CXI1], y0, y1: CE, holes: [cw("choir_s", FcS, CZ, CZI, 8.4, GF + 1.8, 2.0)], pt: PT, panel: cp("choir_s") });
  wall(A, { axis: "x", outer: -CZ, inner: -CZI, aOut: [NX1, CX1], aIn: [NX1, CXI1], y0, y1: CE, holes: [cw("choir_n", FcN, -CZ, -CZI, 8.4, GF + 1.8, 2.0)], pt: PT, panel: cp("choir_n") });
  wall(A, { axis: "z", outer: CX1, inner: CXI1, aOut: [-CZI - PT, CZI + PT], aIn: [-CZI, CZI], y0, y1: CE, holes: [head(window(A, { id: "choir_e", axis: "z", outer: CX1, inner: CXI1, c: 0, sill: GF + 1.6, w: 1.0, h: 2.3, shutters: false }), FcE, -0.5, 0.5, GF + 3.9, { T: NT, glass: true, id: "choir_e" })], pt: PT, panel: cp("choir_e") });
  roofX(A, NX1 - 0.3, CX1, CZ, CE, CR, { gables: [false, true], wallT: NT });
  // ---- arched heads: the voussoir ring, keystone and spandrels (outer stone, inner plaster), a glazed fanlight in
  // the windows; the casements' flat lintels give way to them
  for (const h of arches) {
    const { F, a0, a1, top, rise, T } = h, ac = (a0 + a1) / 2;
    if (h.id) drop(A, (p) => p.t === "b" && !p.r && (p.m === SD || p.m === "oak_x") && Math.abs(p.c[1] - top) < 0.15 && p.s[1] < 0.3 && p.s[F.axis === "x" ? 0 : 2] > a1 - a0 + 0.1 && Math.abs(p.c[F.axis === "x" ? 0 : 2] - ac) < 0.02 && Math.abs(p.c[F.axis === "x" ? 2 : 0] - (F.d - F.out * T / 2)) < T);
    archOpening(A, F, { a0, a1, y1: top, rise, w: h.w ?? 0.2, key: h.key !== false, wall: { e0: -T + PT + 0.005, e1: 0, m: "stone" }, sill: false, fan: h.glass ? -(T - 0.06) : null });
    fpart(A, "arch", F, ac, top + (rise + 0.02) / 2, -T + PT / 2, [a1 - a0, rise + 0.02, PT], "plaster", { th: 0.02, fill: true });
  }
  // ---- portal: two arch orders over the south door on colonnettes; the tower door gets one order
  const portal = (F, a0, a1, top, orders) => {
    for (let i = 0; i < orders; i++) {
      const g = 0.24 * i;
      archOpening(A, F, { a0: a0 - g, a1: a1 + g, y1: top + 0.02, w: 0.22, proj: 0.06 + 0.05 * (orders - i), key: i === orders - 1 });
      for (const a of [a0 - g - 0.13, a1 + g + 0.13]) {
        fpart(A, "cyl", F, a, (GF + top) / 2, 0.08 + 0.05 * (orders - i - 1), [0.15, top - GF, 0.15], SD, { k: 0 });
        fbox(A, F, a - 0.13, a + 0.13, top - 0.14, top + 0.02, -0.01, 0.17 + 0.05 * (orders - i - 1), SD, { bv: 0.012, k: 0 });
        fbox(A, F, a - 0.13, a + 0.13, GF - 0.3, GF + 0.18, -0.01, 0.17 + 0.05 * (orders - i - 1), SD, { bv: 0.012, k: 0 });
      }
    }
  };
  portal(FnS, -3.4, -1.8, GF + 2.8, 2);
  portal(FtS, -11.75, -10.65, GF + 2.3, 1);
  // ---- plinth course, drip course under the windows, cornice under the eaves
  const runs = [[FnS, NX0 + 0.0, NX1, [-3.4 - 0.75, -1.8 + 0.75]], [FnN, NX0, NX1], [FcS, NX1, CX1 + 0.09], [FcN, NX1, CX1 + 0.09], [FtS, TX0 - 0.09, TX1, [-11.75 - 0.5, -10.65 + 0.5]], [FtN, TX0 - 0.09, TX1],
    [FtW, -TZ, TZ], [FcE, -CZ, CZ], [{ axis: "z", d: NX1, out: 1 }, CZ, NZ], [{ axis: "z", d: NX1, out: 1 }, -NZ, -CZ], [{ axis: "z", d: TX1, out: 1 }, TZ, NZ], [{ axis: "z", d: TX1, out: 1 }, -NZ, -TZ]];
  for (const [F, a0, a1, gap] of runs) {
    const holes = gap ? [{ a0: gap[0], a1: gap[1], y0: -9, y1: 9 }] : [];
    for (const [p0, p1] of cutRect(a0, a1, -0.3, y0 + 0.1, holes)) if (p1 - p0 > 0.2) {
      fbox(A, F, p0, p1, -0.3, y0 + 0.1 - (F.axis === "z" ? 0.006 : 0), -0.01, 0.09, "stone_plinth", { bv: 0.02, k: 0 });
      fpart(A, "wedge", F, (p0 + p1) / 2, y0 + 0.125, 0.04, [p1 - p0 - 0.02, 0.05, 0.09], "stone_plinth", { top: 0.2, k: 0 });
    }
  }
  const buttressGaps = [-4.3, -0.8, 2.6].map((x) => ({ a0: x - 0.37, a1: x + 0.37, y0: -9, y1: 99 }));
  band(A, FnS, NX0, NX1, GF + 2.08, 0.15, 0.07, [...buttressGaps, { a0: -3.4 - 0.75, a1: -1.8 + 0.75, y0: -9, y1: 99 }]);
  band(A, FnN, NX0, NX1, GF + 2.08, 0.15, 0.07, buttressGaps);
  for (const [F, a0, a1, y] of [[FnS, NX0, NX1, NE - 0.36], [FnN, NX0, NX1, NE - 0.36], [FcS, NX1, CX1, CE - 0.34], [FcN, NX1, CX1, CE - 0.34]]) cornice(A, F, a0, a1, y, { steps: 2, h: 0.24, proj: 0.16 });
  // ---- furnishings
  pews(A, -6.4, 3.6, -3.0, -0.6, GF); pews(A, -6.4, 3.6, 0.6, 3.0, GF);
  A.beginFurniture("Altar"); A.box([9.2, GF + 0.15, -1.1], [9.9, GF + 1.15, 1.1], SD); A.box([9.15, GF + 1.15, -1.2], [9.95, GF + 1.22, 1.2], "linen"); A.endFurniture();
  A.beginFurniture("Altar candlesticks"); for (const z of [-0.8, 0.8]) A.box([9.5, GF + 1.22, z - 0.03], [9.56, GF + 1.6, z + 0.03], "brass"); A.endFurniture(false);
  A.beginFurniture("Pulpit"); A.box([4.3, GF, 2.2], [5.0, GF + 1.3, 2.95], "oak_y"); A.endFurniture();
  A.beginFurniture("Font"); A.box([-6.6, GF, 2.0], [-6.0, GF + 0.95, 2.6], SD); A.endFurniture();
  table(A, "Vestry table", 7.0, -1.7, 1.0, 0.6, GF + 0.15);
  setNav(A, {
    effect: "block",
    rooms: [room("tower", [TXI0, GF, -TZI], [TXI1, BF - 0.2, TZI]), room("belfry", [TXI0, BF, -TZI], [TXI1, BF + 3.8, TZI]),
            room("nave", [NXI0, GF, -NZI], [NXI1, NE, NZI]), room("choir", [NX1, GF + 0.15, -CZI], [CXI1, CE, CZI])],
    doors: [{ a: "nave", b: "outside", p: [-2.6, GF, NZ - NT / 2] }, { a: "tower", b: "outside", p: [-11.2, GF, TZ - TT / 2] }, { a: "tower", b: "nave", p: [-8.0, GF, 0] },
            { a: "nave", b: "choir", p: [5.6, GF, 0] }, { a: "tower", b: "belfry", p: [-9.3, BF, -2.0] }],
    entrances: [{ p: [-2.6, 0, NZ + 1.2], dir: [0, 0, -1], room: "nave" }, { p: [-11.2, 0, TZ + 1.2], dir: [0, 0, -1], room: "tower" }],
    corners: [...corners("tower", TXI0, TXI1, -TZI, TZI, GF, 0.4), ...corners("belfry", TXI0, TXI1, -TZI, TZI, BF, 0.4), ...corners("nave", NXI0, NXI1, -NZI, NZI, GF, 0.6), ...corners("choir", NX1, CXI1, -CZI, CZI, GF + 0.15, 0.5)],
    firingPoints: [unglazed([-11.2, BF, TZI - 0.35], 0), unglazed([-11.2, BF, -TZI + 0.35], 180), unglazed([TXI0 + 0.35, BF, 0], 270)],
    cover: [cover([-4.3, 0, NZ + 1.2], 180, "high", ["left", "right"]), cover([2.6, 0, -NZ - 1.2], 0, "high", ["left", "right"]), cover([TX0 - 0.6, 0, 0], 90, "high", ["left", "right"])],
  });
  setTour(A, { pos: [-11.2, 0, 7.0], yaw: 0 }, [
    { goto: [-11.2, 4.6], note: "approach the tower door" },
    { interact: "tower_door", aim: [-11.0, 1.3, TZI + 0.05], expectOpen: true },
    { goto: [-11.2, 1.0], expectY: GF, note: "into the tower base" },
    { goto: [-11.2, -2.08] }, { goto: [-13.1, -2.08], note: "foot of the first flight" },
    { goto: [-13.1, 2.0], expectY: GF + 2.6, note: "first landing" }, { goto: [-9.3, 2.0] },
    { goto: [-9.3, -2.08], expectY: GF + 5.2, note: "second landing" }, { goto: [-13.1, -2.08] },
    { goto: [-13.1, 2.0], expectY: GF + 7.8, note: "third landing" }, { goto: [-9.3, 2.0] },
    { goto: [-9.3, -2.0], expectY: BF, note: "belfry floor" }, { goto: [-11.2, -1.95], expectY: BF, note: "at the north arch" },
    { look: [-11.2, BF - 6, -40], hold: 0.8 }, { shot: "mpu_church_belfry_view" },
    { goto: [-9.3, -2.0] }, { goto: [-9.3, 2.0], expectY: GF + 7.8, note: "down" }, { goto: [-13.1, 2.0] }, { goto: [-13.1, -2.08], expectY: GF + 5.2 },
    { goto: [-9.3, -2.08] }, { goto: [-9.3, 2.0], expectY: GF + 2.6 }, { goto: [-13.1, 2.0] }, { goto: [-13.1, -2.08], expectY: GF, note: "back at the tower base" },
    { goto: [-11.2, -2.08] }, { goto: [-11.2, -0.2] }, { goto: [-7.6, 0], note: "through the tower arch" }, { goto: [-2.6, 0.0], expectY: GF, note: "nave" },
    { look: [9.5, 1.5, 0], hold: 0.6 }, { shot: "mpu_church_nave" },
    { goto: [5.6, 0.0] }, { goto: [8.2, 0.0], expectY: GF + 0.15, note: "choir" },
  ]);
  A.meta.ivy = [{ face: "-x", a0: -2.4, a1: 2.4, y0: 0.3, y1: 9.0, density: 0.9 }, { face: "+x", a0: -2.2, a1: 2.2, y0: 0.3, y1: 3.6, density: 0.75 }];
  weather(A, { pocks: [{ F: FtN, a0: -13.8, a1: -8.6, y0: BF + 0.2, y1: BF + 0.8, n: 2 }, { F: FtW, a0: -2.6, a1: 2.6, y0: BF - 0.1, y1: BF + 0.7, n: 2 }] });
  // churchyard: graves north and south of the nave in loose rows, the calvary, a wall with the gate on the south path
  const R = rng(9102), comp = [], G = ["drs_gravestone_round", "drs_gravestone_round", "drs_gravestone_cross", "drs_tomb_chest"];
  const grave = (x, z, yaw) => comp.push({ asset: G[Math.floor(R() * G.length)], pos: [r3(x + (R() - 0.5) * 0.4), 0, r3(z + (R() - 0.5) * 0.3)], yaw: Math.round(yaw + (R() - 0.5) * 8), ground: true, chance: 0.85 });
  for (let x = -6.0; x <= 9.5; x += 1.55) for (const z of [-6.6, -9.4]) grave(x, z, 180);
  for (let x = -7.2; x <= 9.0; x += 1.55) if (Math.abs(x + 2.6) > 1.6) grave(x, 6.0, 0);
  comp.push({ asset: "drs_churchyard_cross", pos: [3.2, 0, 9.4], yaw: 0, ground: true });
  comp.push({ asset: "drs_gate_piers", pos: [-2.6, 0, 11.4], yaw: 0, ground: true });
  for (const s of [-1, 1]) for (let i = 0; i < 3; i++) comp.push({ asset: "drs_garden_wall", pos: [r3(-2.6 + s * (3.9 + 4 * i)), 0, 11.4], yaw: 0, ground: true });
  for (const [x, z] of [[-15.5, 5.5], [12.5, 4.5], [12.0, -6.0], [-15.0, -6.5]]) comp.push({ asset: "nat_hawthorn_bush", pos: [x, 0, z], yaw: Math.round(R() * 360), ground: true, chance: 0.8 });
  for (const [x, z] of [[-6.0, 8.6], [7.0, 8.0], [-12.8, 8.2]]) comp.push({ asset: FLOWERS[Math.floor(R() * FLOWERS.length)], pos: [x, 0, z], yaw: 0, ground: true, chance: 0.6 });
  A.meta.companions = comp;
  return A;
}

export function houses2Assets() {
  return [farmhouse2(), cottage2(), workshop2(), dairy2(), storehouse2(), office2(), school2(), inn2(), warehouse2(), municipal2(), longere2(), barn2(), church2()];
}
