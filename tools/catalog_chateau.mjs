// B3 (docs/asset-format.md#b1-geometry-kit, plan "improve all models"): a Villa-Strassburger-style chateau and ten new
// enterable Normandy 1944 houses, built with the house kit (two-layer walls with real openings, destructible bays,
// hinged doors and casements, stairs with a smooth ramp) and the B1 geometry kit (arches, columns, turrets, cones,
// half-timbering, balconies, dormers, hip and thatch roofs, bevelled trims, ivy, imperfections).
// Regenerate with `node tools/build_mpu_assets.mjs` (never run build_assets.mjs).
//
// One generic builder (`house`) lays every multi-storey building out the same way as tools/catalog_large.mjs: a
// corridor along the rear wall on every level, two independent switchback stair cores at its ends, rooms off the
// corridor. Single-storey houses are a row of rooms joined by doorways near the rear wall. The gatehouse is bespoke.
import { cutRect, wbox, Asset } from "./kit.mjs";
import {
  stdMaterials, wall, partition, window, door, stairs, railing, gableRoof, quoins, pilaster, stringCourse, cornice,
  surround, archOpening, archWindow, colonnade, balcony, dormer, hipRoof, thatchRoof, turret, timberFrame, chimney,
  fbox, weather, interiorTrim,
} from "./parts.mjs";
import { cover, fire, room, corners, setNav, setTour } from "./catalog_atlantic.mjs";

const PT = 0.03, GF = 0.3, CORE_W = 1.2, SPINE = 0.1, ENC = 0.15, LAND = 1.3;
const q4 = (v) => Math.round(v * 1e4) / 1e4;
const SIDES = ["front", "rear", "east", "west"];

function mats(A) {
  stdMaterials(A);
  A.mat("tile", [0.30, 0.12, 0.065], 0.68, "slate", { grain: 2 });          // flat clay tiles (slate courses, tile colour)
  A.mat("render_cream", [0.66, 0.60, 0.47], 0.92, "limewash");
  A.mat("water", [0.035, 0.055, 0.06], 0.08, "plain");
}

// wall frames: kit wall() frame (outer / inner) and B1 facade frame (d / out) of each side, with the jetty j
const wframe = (side, X, Z, T, j = 0) => ({
  front: { axis: "x", outer: Z + j, inner: Z - T + j }, rear: { axis: "x", outer: -Z - j, inner: -Z + T - j },
  east: { axis: "z", outer: X, inner: X - T }, west: { axis: "z", outer: -X, inner: -X + T },
}[side]);
const face = (side, X, Z, j = 0) => ({
  front: { axis: "x", d: Z + j, out: 1 }, rear: { axis: "x", d: -Z - j, out: -1 }, east: { axis: "z", d: X, out: 1 }, west: { axis: "z", d: -X, out: -1 },
}[side]);

// fireplace against a wall face: axis "x" = the face runs along X at depth d (the room toward s along z); "z" = along Z
function fireplace(A, { axis, d, s, a, y, top, w = 1.5, deep = 0.45 }) {
  const B = (a0, a1, y0, y1, e0, e1, m, o) => wbox(A, axis, a0, a1, y0, y1, d + s * e0, d + s * e1, m, o);
  const hw = w / 2, ow = Math.min(0.5, hw - 0.25), oh = 0.95;
  B(a - hw, a - ow, y, top, -0.01, deep, "stone_dressed");
  B(a + ow, a + hw, y, top, -0.01, deep, "stone_dressed");
  B(a - ow, a + ow, y + oh, top, -0.01, deep, "plaster_smoke");
  B(a - ow, a + ow, y, y + oh, -0.01, 0.06, "soot");
  B(a - hw - 0.08, a + hw + 0.08, y + oh + 0.1, y + oh + 0.17, -0.01, deep + 0.08, "oak_x", { bv: 0.01 });   // mantel shelf
  B(a - hw + 0.1, a + hw - 0.1, y, y + 0.02, deep - 0.01, deep + 0.5, "hearthstone", { k: 0 });          // hearth stone
}

// Gable in the facade plane above the eaves (zf, facing s along z) with its own small roof running back into the main
// roof: a triangular wall, two slabs, a ridge roll, barge boards; half-timbered (king post, collar) with a window
function crossGable(A, { x, w, zf, s = 1, y0, rise, back, ov = 0.35, t = 0.12, m = "render", roof = "tile", timber = true, win = true }) {
  const za = zf - s * 0.4, zb = zf + s * 0.02;
  A.prism(2, Math.min(za, zb), Math.max(za, zb), [[x - w / 2, y0], [x + w / 2, y0], [x, y0 + rise]], m);
  const half = w / 2 + ov, th = Math.atan2(rise, w / 2), deg = (th * 180) / Math.PI, Ls = half / Math.cos(th) + 0.08;
  const z0 = zf + s * 0.3, z1 = zf - s * back, cz = (z0 + z1) / 2, lz = Math.abs(z0 - z1);
  for (const sx of [-1, 1]) {
    const mx = x + (sx * half) / 2, my = y0 + rise - (half / 2) * Math.tan(th), n = [sx * Math.sin(th), Math.cos(th)];
    A.obox([mx + (n[0] * t) / 2, my + (n[1] * t) / 2, cz], [Ls, t, lz], [0, 0, -sx * deg], roof);
    A.obox([mx + n[0] * 0.02, my - 0.1, zf + s * 0.33], [Ls, 0.26, 0.04], [0, 0, -sx * deg], "oak_x", { k: 0 });   // barge board
  }
  A.cyl([x, y0 + rise + t + 0.02, cz], [0.18, lz + 0.04, 0.18], "ridge_tile", { r: [90, 0, 0], k: 0 });
  const F = { axis: "x", d: zb, out: s };
  if (timber) {
    fbox(A, F, x - 0.08, x + 0.08, y0 + (win ? 1.75 : 0.05), y0 + rise - 0.25, -0.01, 0.04, "oak_y", { k: 0 });
    const yc = y0 + rise * 0.42, hw = (w / 2) * (1 - (yc - y0) / rise) - 0.05;
    fbox(A, F, x - hw, x + hw, yc - 0.08, yc + 0.08, -0.01, 0.04, "oak_x", { k: 0 });
    fbox(A, F, x - w / 2 + 0.05, x + w / 2 - 0.05, y0 + 0.02, y0 + 0.18, -0.01, 0.045, "oak_x", { k: 0 });
  }
  if (win) {
    const wy0 = y0 + 0.35, wy1 = Math.min(y0 + 1.6, y0 + rise * 0.4), ww = Math.min(0.55, w / 4);
    fbox(A, F, x - ww - 0.07, x + ww + 0.07, wy0 - 0.07, wy1 + 0.07, -0.01, 0.03, "window_paint", { k: 0 });
    fbox(A, F, x - ww, x + ww, wy0, wy1, -0.01, 0.036, "glass", { k: 0 });
    fbox(A, F, x - 0.03, x + 0.03, wy0, wy1, -0.01, 0.045, "window_paint", { k: 0 });
  }
}

// glazed windows on a round / polygonal turret: at angle a (degrees from +x toward +z), height y
function turretWindow(A, cx, cz, r, a, y, w = 0.7, h = 1.2) {
  const ar = (a * Math.PI) / 180, dx = Math.cos(ar), dz = Math.sin(ar), yaw = (Math.atan2(dx, dz) * 180) / Math.PI;
  A.obox([cx + dx * (r - 0.005), y, cz + dz * (r - 0.005)], [w + 0.18, h + 0.18, 0.06], [0, yaw, 0], "stone_dressed", { k: 0, bv: 0.01 });
  A.obox([cx + dx * (r + 0.012), y, cz + dz * (r + 0.012)], [w, h, 0.04], [0, yaw, 0], "glass", { k: 0 });
  A.obox([cx + dx * (r + 0.02), y, cz + dz * (r + 0.02)], [0.05, h, 0.05], [0, yaw, 0], "window_paint", { k: 0 });
}

// Stone-and-brick checkerwork: every outer-layer wall box built with the material "chk" (intact and damaged states)
// becomes an invisible collider plus a grid of visible brick / dressed-stone squares; breached states stay brick.
function checker(A, { Z, y0, cell = [0.6, 0.45] }) {
  const conv = (list, plain, inPanel) => {
    const out = [];
    for (const p of list) {
      if (p.m !== "chk") { out.push(p); continue; }
      if (plain) { out.push({ ...p, m: "brick" }); continue; }
      const L = Math.abs(Math.abs(p.c[2]) + p.s[2] / 2 - Z) < 0.02 ? 0 : 2;
      const lo = p.c[L] - p.s[L] / 2, hi = p.c[L] + p.s[L] / 2, yl = p.c[1] - p.s[1] / 2, yh = p.c[1] + p.s[1] / 2;
      if (!inPanel) out.push({ ...p, m: "brick", v: 0 });   // core: one collider, k:0 squares; bays: the squares collide (the damage field re-meshes bay parts)
      for (let ia = Math.floor((lo + 100) / cell[0] + 1e-6); ia * cell[0] - 100 < hi - 1e-6; ia++)
        for (let iy = Math.floor((yl - y0) / cell[1] + 1e-6); y0 + iy * cell[1] < yh - 1e-6; iy++) {
          const a0 = Math.max(lo, ia * cell[0] - 100), a1 = Math.min(hi, (ia + 1) * cell[0] - 100);
          const b0 = Math.max(yl, y0 + iy * cell[1]), b1 = Math.min(yh, y0 + (iy + 1) * cell[1]);
          if (a1 - a0 < 2e-3 || b1 - b0 < 2e-3) continue;
          const q = { t: "b", c: [...p.c], s: [...p.s], m: (ia + iy) % 2 ? "stone_dressed" : "brick" };
          if (!inPanel) q.k = 0;
          q.c[L] = q4((a0 + a1) / 2); q.s[L] = q4(a1 - a0); q.c[1] = q4((b0 + b1) / 2); q.s[1] = q4(b1 - b0);
          out.push(q);
        }
    }
    list.length = 0; list.push(...out);
  };
  conv(A.parts, false, false);
  for (const pn of A.panels) pn.states.forEach((st, i) => conv(st, i === 2, true));
}

// ============================================================================ the generic house
// c: X, Z (outer half extents), T, h (storey height), n (storeys), attic (an open attic level under a gable roof,
// knee walls `knee` high), jetty (each upper storey oversails front and rear by this much more), style(f) ->
// { m, timber, solin, panel, split: [{a0, a1, m}] (front / rear), east, west (side wall materials), shutters, surround },
// win(f) -> { front, rear, east, west: [pos | {c, w, h, sill}] }, wd(f) -> default {w, h, sill}, parts(f) -> partition
// x list (rooms between the cores; single storey: across the whole house), names(f) -> room ids, hall (index of the
// ground-floor room with the front door), rdx (rear door x), doors: extra doors [{id, side, c, w, h, double, room}],
// party: corridor walls at x with a doorway (terraced pairs), roof: { type: gable | hip | thatch, rise, m, gable },
// ceilingTo (single storey: the ceiling only for x < this), floorMat(x0, x1) (ground floor), dress(A, ctx), cover.
function house(A, c) {
  const { X, Z, h } = c, T = c.T ?? 0.5, XI = X - T, ZI = Z - T, y0 = GF - 0.02;
  const n = c.n ?? 1, attic = !!c.attic, L = n + (attic ? 1 : 0), multi = L > 1;
  const yAt = (k) => GF + k * h, jet = (f) => (c.jetty ?? 0) * f;
  const EAVE = attic ? yAt(n) + (c.knee ?? 0.9) : yAt(n);
  const Zr = Z + jet(L - 1);
  const topOf = (f) => (f === L - 1 ? EAVE - 0.2 : yAt(f + 1) - 0.2);
  const plinth = A.box([-X - 0.06, -(c.plinthDepth ?? 1.0), -Z - 0.06], [X + 0.06, y0, Z + 0.06], "stone_plinth");

  // ---- layout: cores, corridor, rooms
  const nr = Math.round(h / 2 / 0.18), R = nr * 0.27, coreLen = 2 * CORE_W + SPINE;
  const zA = -ZI + (c.corridor ?? 1.8), zEnd = zA + R + LAND, zLand = zA - 0.6, zD = -ZI + 1.1;
  const cores = multi ? [{ xa: -XI, encX: [-XI + coreLen, -XI + coreLen + ENC] }, { xa: XI - coreLen, encX: [XI - coreLen - ENC, XI - coreLen] }] : [];
  for (const k of cores) { k.l1 = [k.xa, k.xa + CORE_W]; k.l2 = [k.xa + CORE_W + SPINE, k.xa + coreLen]; k.l1c = (k.l1[0] + k.l1[1]) / 2; k.l2c = (k.l2[0] + k.l2[1]) / 2; }
  const xL = multi ? cores[0].encX[1] : -XI, xR = multi ? cores[1].encX[0] : XI;
  const levels = [];
  for (let f = 0; f < L; f++) {
    const isAttic = attic && f === L - 1, parts = isAttic ? [] : c.parts(f), b = [xL, ...parts, xR];
    const ids = isAttic ? ["attic"] : b.slice(1).map((_, r) => (c.names?.(f, r)) ?? `f${f}_r${r}`);
    levels.push({ f, y: yAt(f), top: topOf(f), parts, b, ids, isAttic, cx: b.slice(1).map((v, r) => (b[r] + v) / 2) });
  }
  const G = levels[0], hall = c.hall ?? Math.floor(G.cx.length / 2), fdx = G.cx[hall], rdx = c.rdx ?? (multi ? 0 : G.cx[0]);
  const roomAt = (lv, x) => lv.ids[Math.max(0, lv.b.findIndex((v, i) => i > 0 && x <= v) - 1)] ?? lv.ids[0];

  // ---- floors, ceiling
  for (const [x0, x1, m] of c.floorMats ?? [[-XI, XI, "terracotta"]]) A.box([x0, m === "terracotta" ? y0 : GF - 0.05, -ZI], [x1, GF, ZI], m);
  const holes = cores.map((k) => ({ a0: k.xa, a1: k.xa + coreLen, y0: zA, y1: zEnd }));
  for (let f = 1; f < L; f++) for (const [x0, x1, z0, z1] of cutRect(-XI, XI, -ZI - jet(f), ZI + jet(f), holes)) A.box([x0, yAt(f) - 0.2, z0], [x1, yAt(f), z1], "floor_x");
  if (!attic) {
    const cx1 = c.ceilingTo ?? XI;
    A.box([-XI, EAVE - 0.2, -ZI - jet(L - 1)], [cx1, EAVE, ZI + jet(L - 1)], "plaster");
    for (let x = -XI + 0.45; x < cx1 - 0.2; x += 0.9) A.box([x - 0.07, EAVE - 0.36, -ZI - jet(L - 1)], [x + 0.07, EAVE - 0.2, ZI + jet(L - 1)], "oak_z", { k: 0 });
  }

  // ---- stair cores (two independent routes)
  const coreTop = attic ? yAt(L - 1) + 2.4 : EAVE - 0.2;
  for (const k of cores) {
    for (let f = 0; f < L - 1; f++) {
      const y = yAt(f);
      stairs(A, { x0: k.l1[0], x1: k.l1[1], z0: zA, dir: +1, y0: y, rise: h / 2, n: nr, run: R, openSide: 1, core: true });
      A.box([k.xa, y + h / 2 - 0.2, zA + R], [k.xa + coreLen, y + h / 2, zEnd], "tread_x");   // not a floor material: room detection would take the landing for a storey
      stairs(A, { x0: k.l2[0], x1: k.l2[1], z0: zA + R, dir: -1, y0: y + h / 2, rise: h / 2, n: nr, run: R, openSide: 1, core: true });
    }
    A.box([k.l1[1], GF, zA], [k.l2[0], coreTop, zA + R], "plaster");                 // spine
    A.box([k.xa, GF, zEnd], [k.xa + coreLen, coreTop, zEnd + ENC], "plaster");        // end wall
    A.box([k.encX[0], GF, zA], [k.encX[1], coreTop, zEnd + ENC], "plaster");          // enclosure side
    A.box([k.l2[0], GF, zA], [k.l2[1], GF + h / 2 - 0.6, zA + R], "plaster");         // closes the void under the first return
    railing(A, "x", k.l1[0], k.l1[1], zA + 0.04, yAt(L - 1));
  }

  // ---- partitions
  for (const lv of levels) {
    if (lv.isAttic) continue;
    const { y, top, b, cx, f } = lv;
    if (multi) {
      const dh = cx.map((x) => ({ a0: x - 0.5, a1: x + 0.5, y0: y - 1, y1: y + 2.1 }));
      partition(A, { axis: "x", d0: zA, d1: zA + 0.15, a0: xL, a1: xR, y0: y, y1: top, holes: dh });
      for (const x of cx) A.box([x - 0.6, y + 2.1, zA - 0.02], [x + 0.6, y + 2.25, zA + 0.17], "oak_x");
      for (const x of lv.parts) A.box([x - 0.075, y, zA + 0.15], [x + 0.075, top, ZI + jet(f)], "plaster");
      for (const x of c.party ?? []) {
        partition(A, { axis: "z", d0: x - 0.075, d1: x + 0.075, a0: -ZI - jet(f), a1: zA, y0: y, y1: top, holes: [{ a0: zLand - 0.5, a1: zLand + 0.5, y0: y - 1, y1: y + 2.1 }] });
        A.box([x - 0.09, y + 2.1, zLand - 0.6], [x + 0.09, y + 2.25, zLand + 0.6], "oak_z");
      }
    } else {
      for (const x of lv.parts) {
        const tp = x < (c.ceilingTo ?? XI) - 0.01 ? top : EAVE;
        partition(A, { axis: "z", d0: x - 0.075, d1: x + 0.075, a0: -ZI, a1: ZI, y0: y, y1: tp, holes: [{ a0: zD - 0.5, a1: zD + 0.5, y0: y - 1, y1: y + 2.05 }] });
        A.box([x - 0.09, y + 2.05, zD - 0.6], [x + 0.09, y + 2.2, zD + 0.6], "oak_z");
      }
    }
  }

  // ---- doors, windows, walls
  const doorList = [{ id: "front_door", side: "front", c: fdx, w: c.doorW ?? 1.1, h: 2.25, room: G.ids[hall], double: !!c.frontDouble },
    { id: "rear_door", side: "rear", c: rdx, w: 1.0, h: 2.15, room: multi ? "c0" : roomAt(G, rdx) }, ...(c.doors ?? [])];
  const nav = { rooms: [], doors: [], entrances: [], corners: [], firingPoints: [] };
  const winInfo = [];
  for (const lv of levels) {
    const { f, y } = lv, j = jet(f), st = c.style(f), Zo = Z + j, Zi = ZI + j;
    const yb = f === 0 ? y0 : yAt(f) - 0.2, yt = f === L - 1 ? EAVE : yAt(f + 1) - 0.2;
    const H = { front: [], rear: [], east: [], west: [] };
    if (f === 0) for (const d of doorList) {
      const wf = wframe(d.side, X, Z, T);
      const hs = d.side === "front" ? 1 : d.side === "rear" ? -1 : d.side === "east" ? -1 : 1;
      H[d.side].push(door(A, { id: d.id, ...wf, c: d.c, floor: GF, w: d.w ?? 1.0, h: d.h ?? 2.15, hingeSide: d.hinge ?? hs, double: !!d.double, leafMat: d.leafMat }));
    }
    const W = c.win(f), wd = { w: 1.0, h: 1.4, sill: 0.9, ...(c.wd?.(f) ?? {}) };
    for (const side of SIDES) (W[side] ?? []).forEach((p, i) => {
      const o = typeof p === "number" ? { c: p } : p, ww = o.w ?? wd.w, wh = o.h ?? wd.h, sill = y + (o.sill ?? wd.sill);
      const shut = o.shutters ?? st.shutters ?? !st.timber;
      const hole = window(A, { id: `${side}${f}_${i}`, ...wframe(side, X, Z, T, side === "front" || side === "rear" ? j : 0), c: o.c, sill, w: ww, h: wh, shutters: shut, shuttersOpen: o.open ?? true });
      H[side].push(hole);
      if ((o.surround ?? st.surround) && !shut) surround(A, face(side, X, Z, side === "front" || side === "rear" ? j : 0), { a0: o.c - ww / 2, a1: o.c + ww / 2, y0: sill, y1: sill + wh, key: !!st.key });
      winInfo.push({ side, f, c: o.c, sill, top: sill + wh, w: ww });
      const inward = { front: [o.c, y, Zi - 0.6, 0], rear: [o.c, y, -Zi + 0.6, 180], east: [XI - 0.6, y, o.c, 90], west: [-XI + 0.6, y, o.c, 270] }[side];
      nav.firingPoints.push(fire(inward.slice(0, 3), inward[3], "window"));
    });
    const pan = (side, b0) => (st.panel === false ? null : { side, family: st.family ?? (st.timber ? "timber" : "masonry"), bands: [[b0, yt, f, Math.max(yAt(f), b0)]] });
    const yw = st.solin ? GF + st.solin : yb;
    const emit = (side, aOut, aIn, m, suffix = "") => {
      const fr = wframe(side, X, Z, T, side === "front" || side === "rear" ? j : 0);
      if (st.solin) wall(A, { ...fr, aOut, aIn, y0: yb, y1: yw, holes: H[side], stone: "stone", pt: PT, panel: null });
      wall(A, { ...fr, aOut, aIn, y0: yw, y1: yt, holes: H[side], stone: m, pt: PT, panel: pan(side + suffix, yw) });
    };
    for (const side of ["front", "rear"]) {
      const segs = st.split ?? [{ a0: -X, a1: X, m: st.m }];
      segs.forEach((s, i) => emit(side, [Math.max(s.a0, -X), Math.min(s.a1, X)], [Math.max(s.a0, -XI), Math.min(s.a1, XI)], s.m, segs.length > 1 ? `_${i}` : ""));
    }
    emit("east", [-Zi - PT, Zi + PT], [-Zi, Zi], st.east ?? st.m);
    emit("west", [-Zi - PT, Zi + PT], [-Zi, Zi], st.west ?? st.m);
    if (st.timber) for (const side of SIDES) {
      const isX = side === "front" || side === "rear", F = face(side, X, Z, isX ? j : 0);
      const hs = H[side].map((q) => ({ a0: q.a0, a1: q.a1, y0: Math.max(q.y0, yw), y1: q.y1 }));
      timberFrame(A, F, { a0: isX ? -X : -Zo, a1: isX ? X : Zo, y0: yw, y1: yt, holes: hs, rails: yt - yw > 2.4 ? [y + 0.95] : [], sp: c.timberSp ?? 0.8 });
    }
    // jetty: bressummer and joist ends under the oversailing storey
    if (f > 0 && j > jet(f - 1)) for (const s of [1, -1]) {
      const za = s * (Z + jet(f - 1) - 0.05), zb = s * (Z + j + 0.02), yy = yAt(f) - 0.2;
      A.box([-X - 0.02, yy - 0.24, Math.min(za, zb)], [X + 0.02, yy + 0.01, Math.max(za, zb)], "oak_x", { bv: 0.012 });
      for (let x = -X + 0.3; x < X - 0.2; x += 0.6) A.box([x - 0.07, yy - 0.36, Math.min(s * (Z + jet(f - 1) - 0.02), s * (Z + j - 0.06))], [x + 0.07, yy - 0.23, Math.max(s * (Z + jet(f - 1) - 0.02), s * (Z + j - 0.06))], "oak_x", { k: 0 });
    }
  }

  // ---- roof
  const rf = c.roof, RIDGE = EAVE + rf.rise, gm = rf.gable ?? c.style(L - 1).m;
  if (rf.type === "gable") gableRoof(A, { X, Z: Zr, XI: XI, EAVE, RIDGE, ov: rf.ov ?? 0.4, gableMat: gm === "chk" ? "brick" : gm, roofMat: rf.m ?? "tile" });
  else if (rf.type === "thatch") thatchRoof(A, { X, Z: Zr, EAVE, RIDGE, gable: gm });
  else if (rf.type === "hip") {
    const ov = rf.ov ?? 0.45, E = EAVE + 0.42, Rh = E + rf.rise;
    hipRoof(A, { X: X + 0.05, Z: Zr + 0.05, EAVE: E, RIDGE: Rh, ov, m: rf.m ?? "slate" });
  }

  // ---- nav
  const fr0 = [];
  for (const lv of levels) {
    const { f, y, top, b, ids, cx } = lv, j = jet(f);
    if (lv.isAttic) {
      nav.rooms.push(room("attic", [-XI + 0.4, y, -ZI - j + 1.0], [XI - 0.4, y + 2.2, ZI + j - 1.0]));
      nav.corners.push(...corners("attic", -XI + 0.4, XI - 0.4, zLand - 0.2, zA + 2.0, y));
      continue;
    }
    if (multi) {
      nav.rooms.push(room(`c${f}`, [-XI, y, -ZI - j], [XI, top, zA]));
      nav.corners.push(...corners(`c${f}`, -XI, XI, -ZI - j, zA, y));
    }
    ids.forEach((id, r) => {
      const z0 = multi ? zA + 0.15 : -ZI, x0 = b[r] + (r ? 0.075 : 0), x1 = b[r + 1] - (r + 2 < b.length ? 0.075 : 0);
      nav.rooms.push(room(id, [x0, y, z0], [x1, top, ZI + j]));
      nav.corners.push(...corners(id, x0, x1, z0, ZI + j, y));
      if (multi) nav.doors.push({ a: id, b: `c${f}`, p: [q4(cx[r]), y, q4(zA + 0.075)] });
      else if (r > 0) nav.doors.push({ a: ids[r - 1], b: id, p: [q4(b[r]), y, q4(zD)] });
    });
    fr0.push(lv);
  }
  const doorP = (d) => ({ front: [d.c, GF, Z - T / 2], rear: [d.c, GF, -Z + T / 2], east: [X - T / 2, GF, d.c], west: [-X + T / 2, GF, d.c] }[d.side]);
  const entr = (d) => ({ front: [[d.c, 0, Z + 1.0], [0, 0, -1]], rear: [[d.c, 0, -Z - 1.0], [0, 0, 1]], east: [[X + 1.0, 0, d.c], [-1, 0, 0]], west: [[-X - 1.0, 0, d.c], [1, 0, 0]] }[d.side]);
  for (const d of doorList) {
    nav.doors.push({ a: d.room, b: "outside", p: doorP(d).map(q4) });
    const [p, dir] = entr(d);
    nav.entrances.push({ p: p.map(q4), dir, room: d.room });
  }
  nav.cover = c.cover ?? [cover([X + 0.6, 0, Z * 0.45], 270, "high", ["right"]), cover([-X - 0.6, 0, Z * 0.45], 90, "high", ["left"]),
    cover([X + 0.6, 0, -Z * 0.45], 270, "high", ["left"]), cover([-X - 0.6, 0, -Z * 0.45], 90, "high", ["right"])];
  setNav(A, { effect: "block", ...nav });

  // ---- tour: front door -> hall -> (corridor -> core A to the top -> core B down | every room) -> rear door
  const S = [], sh = c.shot;
  S.push({ goto: [fdx, Z + 1.1], note: "approach the front door" });
  S.push({ interact: "front_door", aim: [fdx + 0.25, GF + 1.1, ZI + 0.05], expectOpen: true });
  S.push({ goto: [fdx, ZI - 1.4], expectY: GF, note: "into the " + G.ids[hall] });
  S.push({ look: [fdx, GF + 1.2, multi ? zA : -ZI], hold: 0.5 }, { shot: sh + "_a_hall" });
  if (multi) {
    const [A1, B1] = cores, zH = zA + R + LAND / 2;
    S.push({ goto: [fdx, zA + 0.7] }, { goto: [fdx, zLand], expectY: GF, note: "into the corridor" });
    S.push({ goto: [A1.l1c, zLand] });
    for (let f = 0; f < L - 1; f++) {
      S.push({ goto: [A1.l1c, zH], expectY: yAt(f) + h / 2, note: `core A: level ${f} up to the half landing` });
      S.push({ goto: [A1.l2c, zH] });
      S.push({ goto: [A1.l2c, zLand], expectY: yAt(f + 1), note: `core A reaches level ${f + 1}` });
      if (f === 0) S.push({ look: [A1.l2c + 3, yAt(1) + 1.2, zLand], hold: 0.4 }, { shot: sh + "_b_corridor" });
      if (f + 1 < L - 1) S.push({ goto: [A1.l1c, zLand] });
    }
    const top = levels[L - 1];
    if (top.isAttic) S.push({ goto: [0, zLand], expectY: yAt(L - 1), note: "across the attic" }, { look: [0, yAt(L - 1) + 1.6, 4], hold: 0.6 }, { shot: sh + "_c_attic" });
    else {
      const rw = winInfo.find((w) => w.side === "rear" && w.f === L - 1);
      if (rw) {
        S.push({ goto: [rw.c, -ZI - jet(L - 1) + 0.95], expectY: yAt(L - 1), note: "top-floor rear window" });
        S.push({ interact: `rear${L - 1}_${winInfo.filter((w) => w.side === "rear" && w.f === L - 1).indexOf(rw)}_casement`, aim: [rw.c + 0.25, (rw.sill + rw.top) / 2, -ZI - jet(L - 1) - 0.05], expectOpen: true });
        S.push({ look: [rw.c, yAt(L - 1) - 3, -Z - 20], hold: 0.6 }, { shot: sh + "_c_view" });
        S.push({ goto: [rw.c, zLand] });
      }
      const tr = top.cx[Math.floor(top.cx.length / 2)];
      S.push({ goto: [tr, zLand] }, { goto: [tr, zA + 1.0], expectY: yAt(L - 1), note: "into a top-floor room" }, { look: [tr, yAt(L - 1) + 0.8, ZI], hold: 0.5 }, { shot: sh + "_d_room" }, { goto: [tr, zLand] });
    }
    S.push({ goto: [B1.l2c, zLand] });
    for (let f = L - 1; f > 0; f--) {
      S.push({ goto: [B1.l2c, zH], expectY: yAt(f) - h / 2, note: `core B: level ${f} down to the half landing` });
      S.push({ goto: [B1.l1c, zH] });
      S.push({ goto: [B1.l1c, zLand], expectY: yAt(f - 1), note: `core B reaches level ${f - 1}` });
      if (f > 1) S.push({ goto: [B1.l2c, zLand] });
    }
    S.push({ goto: [rdx, zLand], expectY: GF });
  } else {
    S.push({ goto: [fdx, zD] });
    const order = [...G.cx.keys()].filter((r) => r < hall).reverse().concat([...G.cx.keys()].filter((r) => r > hall));
    for (const r of order) S.push({ goto: [G.cx[r], zD], expectY: GF, note: "through to the " + G.ids[r] });
    S.push({ look: [G.cx[order.at(-1) ?? hall], GF + 1.0, ZI], hold: 0.4 }, { shot: sh + "_b_room" });
    S.push({ goto: [rdx, zD], expectY: GF });
  }
  S.push({ interact: "rear_door", aim: [rdx - 0.2, GF + 1.1, -ZI - 0.05], expectOpen: true });
  S.push({ goto: [rdx, -Z - 1.6], note: "leave by the rear door" });
  setTour(A, { pos: [fdx, 0, Zr + (c.startOut ?? 5.5)], yaw: 0 }, S, c.tourExtra ?? {});

  const ctx = { X, Z, T, XI, ZI, EAVE, RIDGE, Zr, yAt, jet, L, h, zA, zEnd, xL, xR, fdx, rdx, levels, plinth, cores };
  c.dress?.(A, ctx);
  if (c.checker) checker(A, { Z, y0 });
  weather(A, { keep: [plinth] });
  interiorTrim(A, { levels: Array.from({ length: L }, (_, k) => yAt(k)) });   // GX2: skirting, dado rail, doorway jambs
  return ctx;
}

// common masonry dressing: quoins, string courses at each floor (B1 frames), optional cornice under a hip roof
function masonry(A, { X, Z, yAt, L, EAVE }, { q = true, upTo = EAVE, sc = true, cor = false, from = 1, to = L } = {}) {
  if (q) quoins(A, X, Z, GF - 0.02, upTo);
  if (sc) for (let f = from; f < to; f++) for (const s of SIDES) {
    const isX = s === "front" || s === "rear";
    stringCourse(A, face(s, X, Z), isX ? -X - 0.06 : -Z, isX ? X + 0.06 : Z, yAt(f) - 0.28);
  }
  if (cor) for (const s of SIDES) { const isX = s === "front" || s === "rear"; cornice(A, face(s, X, Z), isX ? -X : -Z, isX ? X : Z, EAVE - 0.36, { ext: isX }); }
}
const comp = (asset, x, z, yaw = 0) => ({ asset, pos: [x, 0, z], yaw });
function meta(id, name, category, description, floors, footprint, extra = {}) {
  return new Asset({ id, version: 2, name, category, description, floors, enterable: true, footprint, plinthDepth: 1.2, maxGrade: 0.8, ...extra });   // GX2 v2: interior trim
}

// ============================================================================ vil_chateau_grand
// Villa Strassburger (Deauville) style: 40 x 22 m, three storeys and an attic. Stone-and-brick checkerwork ground
// floor, half-timbered upper storeys, a 45 degree tiled roof with three cross gables and dormers, a round corner tower
// with an 11 m conical spire, a slim octagonal turret, an arcaded gallery with a balcony over it, railed balconies,
// tall brick chimney stacks, ivy. Hall, salon, library, dining room and kitchen; twelve bedrooms; an open attic.
export function chateauGrand() {
  const X = 20, Z = 11;
  const A = meta("vil_chateau_grand", "Grand chateau (Deauville villa)", "Landmarks",
    "Norman seaside chateau in the style of the Villa Strassburger at Deauville, 40 x 22 m: a stone-and-brick checkerwork ground floor under two half-timbered storeys and a steep 45 degree tiled roof with three cross gables and dormers, a round corner tower with an 11 m conical spire and a slim octagonal turret, an arcaded gallery along the front carrying a balcony, railed balconies, tall brick chimney stacks and ivy. Enterable: entrance hall, salon, library, dining room and kitchen off a rear corridor, two stair cores to eight bedrooms on two floors and an open attic.",
    4, [49, 29], { ivy: [{ face: "+x", a0: -7, a1: 6, y0: 0.4, y1: 3.6 }, { face: "-z", a0: -19, a1: -8, y0: 0.4, y1: 7.0, density: 0.8 }, { face: "-x", a0: -3, a1: 9, y0: 0.4, y1: 5.5 }, { face: "+z", a0: 13, a1: 18.5, y0: 0.4, y1: 3.4, density: 0.6 }] });
  mats(A);
  const big = { w: 1.3, h: 2.1, sill: 0.65 }, up = { w: 1.2, h: 1.75, sill: 0.8 };
  const ctx = house(A, {
    X, Z, T: 0.6, h: 3.6, n: 3, attic: true, knee: 1.0, corridor: 2.2, checker: true, shot: "b3_chateau",
    style: (f) => (f === 0 ? { m: "chk", surround: true, key: true, shutters: false } : { m: "render_cream", timber: true, shutters: false, panel: f < 3 ? undefined : false }),
    wd: (f) => (f === 0 ? big : up),
    win: (f) => (f === 3 ? {} : f === 0 ? {
      front: [-18.1, -14.6, -11.6, -7.8, -4.7, -1.9, 1.9, 4.7, 7.8, 11.6, 14.6, 18.1],
      rear: [-17.5, -12.5, -8.0, -3.0, 3.0, 8.0, 12.5, 17.5], east: [-1.0, 3.5, 8.0], west: [-1.0, 3.5, 8.0],
    } : {
      front: [-18.1, -14.6, -10.9, -6.0, -2.4, 2.4, 6.0, 10.9, 14.6, 18.1], rear: [-17.5, -12.5, -8.0, -3.0, 0, 3.0, 8.0, 12.5, 17.5], east: [-1.0, 3.5, 8.0], west: [-1.0, 3.5, 8.0],
    }),
    parts: (f) => (f === 0 ? [-9.5, -3, 3, 9.5] : [-8.4, 0, 8.4]),   // 16 rooms in all: the room-detection cap
    names: (f, r) => (f === 0 ? ["salon", "library", "hall", "dining", "kitchen"][r] : `bedroom${f}_${r + 1}`),
    hall: 2, rdx: 0, doorW: 1.4, frontDouble: true,
    roof: { type: "gable", rise: 10.5, m: "tile", gable: "render_cream", ov: 0.45 },
    cover: [cover([-X - 0.6, 0, 3.0], 90, "high", ["left"]), cover([-X - 0.6, 0, -3.0], 90, "high", ["right"]), cover([X + 0.6, 0, -3.0], 270, "high", ["left"]),
      cover([X + 0.6, 0, 3.0], 270, "high", ["right"]), cover([-10, 0, -Z - 0.6], 0, "high", ["left", "right"]), cover([10, 0, -Z - 0.6], 0, "high", ["left", "right"])],
    startOut: 8,
    dress: (A, k) => {
      const { EAVE, RIDGE, yAt, zA } = k, fr = face("front", X, Z), pitch = (RIDGE - EAVE) / Z, surf = (z) => RIDGE + 0.14 - Math.abs(z) * pitch;
      // ground floor dressing: dressed plinth course, quoins to the first floor, a string course under the timbering
      quoins(A, X, Z, GF - 0.02, yAt(1) - 0.2);
      for (const s of SIDES) { const isX = s === "front" || s === "rear"; stringCourse(A, face(s, X, Z), isX ? -X - 0.06 : -Z, isX ? X + 0.06 : Z, yAt(1) - 0.36, { h: 0.2, proj: 0.08 }); }
      // terrace and arcaded gallery with a balcony over it
      A.box([-13.2, -0.6, Z - 0.05], [13.2, GF - 0.02, Z + 3.1], "stone_dressed", { bv: 0.02 });
      A.box([-13.2, -0.3, Z + 3.1], [13.2, 0.14, Z + 3.5], "stone_dressed", { bv: 0.02 });
      colonnade(A, { axis: "x", a0: -12, a1: 12, d: Z + 2.5, y0: GF - 0.02, y1: 3.73, n: 14, r: 0.2, depth: 0.5 });
      balcony(A, fr, { a0: -12.6, a1: 12.6, y: 3.92, depth: 2.9, t: 0.2 });
      // corner tower (round, tall conical spire) and the slim octagonal turret
      turret(A, { x: X + 1.8, z: Z + 1.8, y0: 0, y1: EAVE + 2.6, r: 2.6, spire: 11, roof: "tile", m: "stone" });
      for (let f = 0; f < 4; f++) for (const a of [10, 45, 80]) turretWindow(A, X + 1.8, Z + 1.8, 2.6, a, yAt(f) + 1.6, 0.6, f < 3 ? 1.3 : 0.9);
      turret(A, { x: -X - 1.0, z: -Z - 1.0, y0: 0, y1: EAVE + 4.5, r: 1.5, spire: 6.5, sides: 8, roof: "slate", m: "stone_dressed" });
      for (let f = 1; f < 4; f++) turretWindow(A, -X - 1.0, -Z - 1.0, 1.5, 225, yAt(f) + 1.5, 0.4, 0.9);
      // balconies
      for (const a of [-6.0, 6.0]) balcony(A, fr, { a0: a - 1.0, a1: a + 1.0, y: yAt(2), depth: 0.8 });
      balcony(A, face("east", X, Z), { a0: 2.3, a1: 4.7, y: yAt(1), depth: 0.85 });
      balcony(A, face("west", X, Z), { a0: -2.2, a1: 0.2, y: yAt(2), depth: 0.8 });
      // cross gables and dormers
      for (const x of [-11, 0, 11]) crossGable(A, { x, w: 7, zf: Z, y0: EAVE, rise: 5.2, back: Z - (Z - 5.2 / pitch) + 0.6 });
      for (const [x, s] of [[-5.5, 1], [5.5, 1], [-17, 1], [17, 1], [-12, -1], [-4.5, -1], [0, -1], [4.5, -1], [12, -1]]) dormer(A, { x, z: s * (Z - 1.0), y: surf(Z - 1.0) - 0.05, w: 1.3, h: 1.35, depth: 1.8, s, roof: "tile", m: "render_cream" });
      // tall brick chimney stacks on the rear wall and through the ridge
      for (const x of [-15, -5.5, 5.5, 15]) chimney(A, { x, z: -Z - 0.05, y0: GF, y1: EAVE + 6.2, w: 1.0, d: 0.7, m: "brick", pots: 3 });
      for (const x of [-8, 8]) chimney(A, { x, z: 0, y0: yAt(3), y1: RIDGE + 2.2, w: 1.1, d: 0.7, m: "brick", pots: 3 });
      // fireplaces (the kitchen's is the hearth room)
      for (const [x, w] of [[-15.4, 1.5], [-4.6, 1.5], [8.0, 1.5], [15.2, 2.0]]) fireplace(A, { axis: "x", d: zA + 0.15, s: 1, a: x, y: GF, top: yAt(1) - 0.2, w });
      for (const x of [-15.0, 15.0]) for (const f of [1, 2]) fireplace(A, { axis: "x", d: zA + 0.15, s: 1, a: x, y: yAt(f), top: yAt(f + 1) - 0.2, w: 1.2 });
    },
  });
  A.meta.companions = [comp("tree_oak", -27, 15), comp("tree_ash", 27, -15, 40), comp("shrub_hazel", -16, 14.8), comp("shrub_hazel", 16.5, 15.2, 90),
    comp("nat_flowers_poppy", -8, 15.2), comp("nat_flowers_cornflower", 8, 15.2), comp("nat_hawthorn_bush", -24, -6), comp("nat_flowers_foxglove", 25, 4)];
  void ctx;
  return A;
}

// ============================================================================ 1. thatched half-timbered longhouse
export function longhouse() {
  const X = 8.5, Z = 3.6;
  const A = meta("vil_longhouse_thatched", "Thatched longhouse (colombage)", "Houses",
    "Norman longhouse, 17 x 7.2 m: half-timbered colombage walls on a rubble-stone sill wall under a deep thatched roof with a round ridge roll. Kitchen with an open hearth and chimney stack (west), the salle (middle), and the byre with its own double door (east); rooms joined by doorways along the rear wall. Shuttered casements, ivy on the west gable.",
    1, [18.2, 9.4], { ivy: [{ face: "-x", a0: -2.6, a1: 2.6, y0: 0.45, y1: 2.9 }] });
  mats(A);
  house(A, {
    X, Z, T: 0.45, h: 2.95, n: 1, shot: "b3_longhouse",
    style: () => ({ m: "render", timber: true, solin: 0.65, family: "timber" }), wd: () => ({ w: 0.85, h: 1.0, sill: 0.95 }),
    win: () => ({ front: [-6.6, -4.2, -1.9, 1.6, { c: 7.3, w: 0.6, h: 0.7, sill: 1.2 }], rear: [-4.0, 0.8, 5.3], west: [-1.6] }),
    parts: () => [-2.8, 2.6], names: (f, r) => ["kitchen", "salle", "byre"][r], hall: 1, rdx: -6.3,
    doors: [{ id: "byre_door", side: "front", c: 5.2, w: 1.6, h: 2.1, double: true, room: "byre" }],
    roof: { type: "thatch", rise: 4.4, gable: "render" }, timberSp: 0.75,
    dress: (A, { XI, EAVE, RIDGE }) => {
      fireplace(A, { axis: "z", d: -XI, s: 1, a: 0, y: GF, top: EAVE - 0.2, w: 1.9, deep: 0.55 });
      chimney(A, { x: -X + 0.15, z: 0, y0: EAVE - 0.6, y1: RIDGE + 1.4, w: 0.55, d: 0.8 });
    },
  });
  A.meta.companions = [comp("tree_apple", -6, 9.8), comp("tree_apple", 4.5, 10.6, 70), comp("shrub_hazel", 10.8, -2), comp("nat_flowers_cowparsley", -2.6, 5.6), comp("nat_hawthorn_bush", -11.5, 1)];
  return A;
}

// ============================================================================ 2. jettied half-timbered townhouse
export function townhouseJettied() {
  const X = 5.2, Z = 4.2;
  const A = meta("vil_townhouse_jettied", "Half-timbered townhouse (jettied)", "Houses",
    "Norman town house, 10.4 x 8.4 m: a rubble-stone ground floor (shop room) under two half-timbered storeys, each jettied 35 cm further out over the street and the yard on oak bressummers and joist ends, under a steep tiled gable roof with a brick chimney. Two stairs, a rear corridor on every floor, parlour and bedroom above.",
    3, [11.4, 10.6]);
  mats(A);
  house(A, {
    X, Z, T: 0.45, h: 3.0, n: 3, jetty: 0.35, shot: "b3_townhouse",
    style: (f) => (f === 0 ? { m: "stone" } : { m: "render_cream", timber: true }),
    wd: (f) => (f === 0 ? { w: 1.0, h: 1.6, sill: 0.7 } : { w: 0.9, h: 1.4, sill: 0.85 }),
    win: (f) => (f === 0 ? { front: [-3.6, -1.5, 1.5, 3.6], rear: [-2.6, 2.6] } : { front: [-3.5, -1.2, 1.2, 3.5], rear: [-2.6, 0, 2.6], east: [2.7], west: [2.7] }),
    parts: () => [], names: (f) => ["shop", "parlour", "bedroom"][f], rdx: 0,
    roof: { type: "gable", rise: 5.2, m: "tile", gable: "render_cream" },
    dress: (A, k) => {
      quoins(A, X, Z, GF - 0.02, k.yAt(1) - 0.2);
      chimney(A, { x: X - 0.15, z: -1.0, y0: k.EAVE - 0.4, y1: k.RIDGE + 0.9, w: 0.55, d: 0.8, m: "brick" });
    },
  });
  A.meta.companions = [comp("nat_flowers_daisy", -4.4, 5.9)];
  return A;
}

// ============================================================================ 3. Deauville brick-and-timber villa
export function villaDeauville() {
  const X = 7.5, Z = 5.3;
  const A = meta("vil_villa_deauville", "Deauville seaside villa", "Houses",
    "Belle Epoque seaside villa of the Deauville coast, 15 x 10.6 m: a brick ground floor with stone dressings, a half-timbered first floor and a steep tiled roof with a cross gable and dormers; a timber porch over the front door, railed balconies on the front and the east side, a lateral brick chimney. Salon, hall and dining room; two bedrooms; an open attic; two stairs.",
    3, [16.6, 13.4], { ivy: [{ face: "+z", a0: -7.4, a1: -5.0, y0: 0.4, y1: 5.0 }, { face: "-x", a0: -4.0, a1: 1.0, y0: 0.4, y1: 3.0 }] });
  mats(A);
  house(A, {
    X, Z, T: 0.45, h: 3.2, n: 2, attic: true, knee: 0.8, shot: "b3_villa",
    style: (f) => (f === 0 ? { m: "brick", surround: true, shutters: false } : { m: "render_cream", timber: true, panel: f === 2 ? false : undefined }),
    wd: (f) => (f === 0 ? { w: 1.2, h: 2.0, sill: 0.5 } : { w: 1.0, h: 1.5, sill: 0.8 }),
    win: (f) => (f === 2 ? {} : f === 0 ? { front: [-6.2, -2.85, 2.95, 6.2], rear: [-5.5, 2.5, 5.5], east: [2.7], west: [2.7] } : { front: [-6.2, -2.2, 2.2, 6.2], rear: [-5.5, -1.5, 2.5, 5.5], east: [2.7], west: [2.7] }),
    parts: (f) => (f === 0 ? [-1.3, 1.5] : [0]), names: (f, r) => (f === 0 ? ["salon", "hall", "dining"][r] : `bedroom_${r + 1}`), hall: 1, rdx: -1.0, doorW: 1.2,
    roof: { type: "gable", rise: 6.8, m: "tile", gable: "render_cream" },
    dress: (A, k) => {
      const { EAVE, RIDGE, yAt } = k, fr = face("front", X, Z), pitch = (RIDGE - EAVE) / Z, surf = (z) => RIDGE + 0.14 - Math.abs(z) * pitch;
      quoins(A, X, Z, GF - 0.02, yAt(1) - 0.2);
      for (const s of SIDES) { const isX = s === "front" || s === "rear"; stringCourse(A, face(s, X, Z), isX ? -X - 0.06 : -Z, isX ? X + 0.06 : Z, yAt(1) - 0.34, { h: 0.18 }); }
      balcony(A, fr, { a0: -3.2, a1: -1.4, y: yAt(1), depth: 0.9 });
      balcony(A, face("east", X, Z), { a0: 1.6, a1: 3.8, y: yAt(1), depth: 0.85 });
      // porch: oak posts and a tiled lean-to over the front door
      for (const x of [-0.95, 1.15]) A.cyl([x, 1.4, Z + 1.75], [0.16, 2.8, 0.16], "oak_y", { n: 8 });
      A.box([-1.25, 2.72, Z + 1.6], [1.45, 2.86, Z + 1.9], "oak_x", { bv: 0.01 });
      A.obox([0.1, 3.0, Z + 1.0], [2.9, 0.12, 2.2], [11.5, 0, 0], "tile");
      A.box([-1.5, -0.3, Z + 0.04], [1.7, 0.14, Z + 2.1], "stone_dressed", { bv: 0.015 });
      crossGable(A, { x: 3.2, w: 4.4, zf: Z, y0: EAVE, rise: 3.8, back: 3.8 / pitch + 0.6 });
      dormer(A, { x: -3.2, z: Z - 0.9, y: surf(Z - 0.9) - 0.05, w: 1.1, h: 1.2, depth: 1.6, s: 1, roof: "tile", m: "render_cream" });
      for (const x of [-3, 3]) dormer(A, { x, z: -(Z - 0.9), y: surf(Z - 0.9) - 0.05, w: 1.1, h: 1.2, depth: 1.6, s: -1, roof: "tile", m: "render_cream" });
      chimney(A, { x: 0.6, z: -Z - 0.05, y0: GF, y1: EAVE + 4.6, w: 0.9, d: 0.7, m: "brick" });
      fireplace(A, { axis: "x", d: k.zA + 0.15, s: 1, a: -3.6, y: GF, top: yAt(1) - 0.2, w: 1.3 });
    },
  });
  A.meta.companions = [comp("shrub_hazel", -9.6, 4), comp("nat_flowers_poppy", 3.6, 6.4), comp("nat_flowers_daisy", -3.8, 6.4), comp("tree_poplar", 11, -5)];
  return A;
}

// ============================================================================ 4. stone manor with a round stair turret
export function manorTurret() {
  const X = 8.5, Z = 5.0;
  const A = meta("vil_manor_turret", "Stone manor with stair turret", "Houses",
    "Norman manor house (manoir), 17 x 10 m, of dressed rubble stone with quoins, string course and architrave windows, under a steep slate roof between stone gables with tall chimneys; a round stair turret with slit windows and a conical spire stands against the front. Hall, salon and dining room with fireplaces; three bedrooms; two stairs; ivy.",
    2, [18.2, 14], { ivy: "auto" });
  mats(A);
  const tx = -6.5, tz = Z + 1.15, tr = 1.45;
  house(A, {
    X, Z, T: 0.55, h: 3.2, n: 2, shot: "b3_manor",
    style: () => ({ m: "stone", surround: true, shutters: false }), wd: (f) => (f === 0 ? { w: 1.2, h: 1.7, sill: 0.8 } : { w: 1.1, h: 1.5, sill: 0.85 }),
    win: (f) => (f === 0 ? { front: [-3.45, 3.45, 6.6], rear: [-5.5, -2.5, 2.5, 5.5], east: [2.85], west: [2.85] } : { front: [-3.45, 0, 3.45, 6.6], rear: [-5.5, -2.5, 2.5, 5.5], east: [2.85], west: [2.85] }),
    parts: () => [-1.6, 1.6], names: (f, r) => (f === 0 ? ["salon", "hall", "dining"][r] : `bedroom_${r + 1}`), hall: 1, rdx: 0,
    roof: { type: "gable", rise: 5.5, m: "slate", gable: "stone" },
    dress: (A, k) => {
      masonry(A, k);
      turret(A, { x: tx, z: tz, y0: 0, y1: k.EAVE + 2.2, r: tr, spire: 4.8, roof: "slate" });
      for (const [a, y] of [[60, 1.6], [90, 3.4], [120, 5.2], [90, 7.4]]) turretWindow(A, tx, tz, tr, a, y, 0.18, 0.8);
      for (const sx of [-1, 1]) chimney(A, { x: sx * (X - 0.17), z: -1.5, y0: k.EAVE - 0.5, y1: k.RIDGE + 1.0, w: 0.55, d: 0.9, m: "stone" });
      for (const x of [-4.6, 4.6]) fireplace(A, { axis: "x", d: k.zA + 0.15, s: 1, a: x, y: GF, top: k.yAt(1) - 0.2, w: 1.6 });
    },
    cover: [cover([X + 0.6, 0, 2.2], 270, "high", ["right"]), cover([X + 0.6, 0, -2.2], 270, "high", ["left"]), cover([-X - 0.6, 0, -2.2], 90, "high", ["right"]), cover([-X - 0.6, 0, 2.6], 90, "high", ["left"])],
  });
  A.meta.companions = [comp("tree_oak", -13.5, 7.5), comp("shrub_hazel", 11.2, 3), comp("nat_hawthorn_bush", -11.5, -4), comp("nat_flowers_foxglove", 2.8, 6.3)];
  return A;
}

// ============================================================================ 5. brick-and-stone corner shop
export function cornerShop() {
  const X = 5.5, Z = 4.6;
  const A = meta("vil_corner_shop", "Corner shop with apartment", "Houses",
    "Brick-and-stone corner shop (epicerie), 11 x 9.2 m: brick walls with dressed-stone quoins, string course and cornice under a slate hip roof; a ground-floor shop with display windows, a painted fascia and canvas awnings on the street front and a side door on the corner, the shopkeeper's apartment above with shuttered windows. Two stairs.",
    2, [12.2, 11]);
  mats(A);
  house(A, {
    X, Z, T: 0.45, h: 3.2, n: 2, shot: "b3_shop",
    style: (f) => ({ m: "brick", shutters: f > 0 }), wd: (f) => (f === 0 ? { w: 1.0, h: 2.0, sill: 0.5 } : { w: 1.0, h: 1.5, sill: 0.85 }),
    win: (f) => (f === 0 ? { front: [-3.8, { c: -1.65, w: 0.9 }, { c: 1.65, w: 0.9 }, 3.8], rear: [-3.5], west: [2.8] } : { front: [-3.8, -1.3, 1.3, 3.8], rear: [-3.5, 0, 3.5], east: [2.8], west: [2.8] }),
    parts: () => [], names: (f) => ["shop", "apartment"][f], rdx: 1.5,
    doors: [{ id: "corner_door", side: "east", c: 2.8, w: 1.0, h: 2.2, room: "shop" }],
    roof: { type: "hip", rise: 3.6, m: "slate" },
    dress: (A, k) => {
      masonry(A, k, { cor: true });
      const fr = face("front", X, Z), ea = face("east", X, Z);
      fbox(A, fr, -X + 0.3, X - 0.3, 2.8, 3.25, -0.01, 0.07, "door_paint", { bv: 0.01 });   // fascia (sign board)
      fbox(A, ea, 0.6, Z - 0.3, 2.8, 3.25, -0.01, 0.07, "door_paint", { bv: 0.01 });
      chimney(A, { x: -2.0, z: -1.6, y0: k.EAVE, y1: k.EAVE + 0.42 + 3.6 + 0.9, w: 0.8, d: 0.6, m: "brick" });
      for (const x of [-3.8, -1.65, 1.65, 3.8]) A.obox([x, 2.62, Z + 0.45], [x === -3.8 || x === 3.8 ? 1.3 : 1.2, 0.05, 0.95], [28, 0, 0], "canvas", { k: 0 });
    },
  });
  return A;
}

// ============================================================================ 6. stone chapel-style gatehouse
export function gatehouse() {
  const X = 6.4, Z = 3.6, T = 0.6, XI = X - T, ZI = Z - T, y0 = GF - 0.02, P = 1.7, PW = 0.3;
  const ys = GF + 2.3, EAVE = GF + 4.9, RIDGE = EAVE + 4.6;
  const A = meta("vil_gatehouse_chapel", "Chapel gatehouse", "Houses",
    "Stone gatehouse in the manner of a small chapel, 12.8 x 7.2 m: a round-arched carriage passage with a barrel vault runs through the middle under a steep slate roof crowned by a bellcote; the porter's lodge opens off the passage on one side, a small chapel with lancet windows, an altar and its own front door on the other.",
    1, [13.8, 9.4], { ivy: [{ face: "-x", a0: -3.0, a1: 3.0, y0: 0.4, y1: 4.5 }, { face: "+z", a0: -6.3, a1: -2.6, y0: 0.4, y1: 3.0, density: 0.7 }] });
  mats(A);
  const plinth = A.box([-X - 0.06, -1.0, -Z - 0.06], [-P, y0, Z + 0.06], "stone_plinth");
  A.box([P, -1.0, -Z - 0.06], [X + 0.06, y0, Z + 0.06], "stone_plinth");
  A.box([-P, -0.4, -Z - 0.3], [P, 0.03, Z + 0.3], "stone_plinth", { bv: 0.01 });   // passage setts
  A.box([-XI, y0, -ZI], [-P - PW, GF, ZI], "terracotta");
  A.box([P + PW, GF - 0.05, -ZI], [XI, GF, ZI], "stone_dressed");                  // chapel flags
  A.box([-XI, EAVE - 0.2, -ZI], [-P, EAVE, ZI], "plaster");
  A.box([P, EAVE - 0.2, -ZI], [XI, EAVE, ZI], "plaster");
  const fr = wframe("front", X, Z, T), bk = wframe("rear", X, Z, T), arch = { a0: -P, a1: P, y0: -10, y1: ys + P + 0.02 };
  const front = [arch, window(A, { id: "lodge_front", ...fr, c: -3.9, sill: GF + 0.9, w: 0.9, h: 1.2 }), door(A, { id: "chapel_front_door", ...fr, c: 3.9, floor: GF, w: 1.0, h: 2.2, hingeSide: 1 })];
  // chapel lancets: fixed leaded glass in round-headed openings (the spandrels close the rectangular holes)
  const lancet = (F, a, sill, w, top) => {
    fbox(A, F, a - w / 2, a + w / 2, sill, top, -T / 2 - 0.01, -T / 2 + 0.01, "glass");
    for (const y of [sill + (top - sill) * 0.33, sill + (top - sill) * 0.66]) fbox(A, F, a - w / 2, a + w / 2, y - 0.012, y + 0.012, -T / 2 - 0.02, -T / 2 + 0.02, "iron", { k: 0 });
    fbox(A, F, a - w / 2, a + w / 2, sill - 0.06, sill, -T, 0.04, "stone_dressed", { bv: 0.01 });
    return { a0: a - w / 2, a1: a + w / 2, y0: sill, y1: top + 0.02 };
  };
  const back = [arch, window(A, { id: "lodge_rear", ...bk, c: -3.9, sill: GF + 0.9, w: 0.9, h: 1.2 }), lancet(face("rear", X, Z), 3.9, GF + 1.2, 0.7, GF + 2.45 + 0.35)];
  const east = [lancet(face("east", X, Z), 0, GF + 1.3, 0.8, GF + 2.8 + 0.4)];
  const west = [window(A, { id: "lodge_west", ...wframe("west", X, Z, T), c: 0, sill: GF + 0.9, w: 0.9, h: 1.2 })];
  const pn = (side) => ({ side, family: "masonry", bands: [[y0, EAVE, 0, GF]] });
  wall(A, { ...fr, aOut: [-X, X], aIn: [-XI, XI], y0, y1: EAVE, holes: front, pt: PT, panel: pn("front") });
  wall(A, { ...bk, aOut: [-X, X], aIn: [-XI, XI], y0, y1: EAVE, holes: back, pt: PT, panel: pn("rear") });
  wall(A, { ...wframe("east", X, Z, T), aOut: [-ZI - PT, ZI + PT], aIn: [-ZI, ZI], y0, y1: EAVE, holes: east, pt: PT, panel: pn("east") });
  wall(A, { ...wframe("west", X, Z, T), aOut: [-ZI - PT, ZI + PT], aIn: [-ZI, ZI], y0, y1: EAVE, holes: west, pt: PT, panel: pn("west") });
  // passage walls with the lodge and chapel doors, the barrel vault, the arch faces
  const ld = door(A, { id: "lodge_door", axis: "z", outer: -P, inner: -P - PW, c: 0, floor: GF, w: 1.0, h: 2.1, hingeSide: 1 });
  const cd = door(A, { id: "chapel_door", axis: "z", outer: P, inner: P + PW, c: 0, floor: GF, w: 1.0, h: 2.1, hingeSide: -1 });
  partition(A, { axis: "z", d0: -P - PW, d1: -P, a0: -ZI, a1: ZI, y0: 0.02, y1: EAVE - 0.2, holes: [ld], m: "stone" });
  partition(A, { axis: "z", d0: P, d1: P + PW, a0: -ZI, a1: ZI, y0: 0.02, y1: EAVE - 0.2, holes: [cd], m: "stone" });
  A.arch([0, ys + (P + 0.25) / 2, 0], [2 * (P + 0.25), P + 0.25, 2 * ZI], "stone_dressed", { th: 0.25 });
  for (const F of [face("front", X, Z), face("rear", X, Z)]) archOpening(A, F, { a0: -P, a1: P, y1: ys, w: 0.3, wall: { e0: -T, e1: 0 }, jambs: 0.03 });
  archWindow(A, face("east", X, Z), { a0: -0.4, a1: 0.4, y1: GF + 2.8, rise: 0.4, jambs: GF + 1.3, wall: { e0: -T, e1: 0 }, fan: null, sill: false });
  archWindow(A, face("rear", X, Z), { a0: 3.55, a1: 4.25, y1: GF + 2.45, rise: 0.35, jambs: GF + 1.2, wall: { e0: -T, e1: 0 }, fan: null, sill: false });
  archOpening(A, face("front", X, Z), { a0: 3.4, a1: 4.4, y1: GF + 2.2, rise: 0.5, w: 0.18, jambs: 0.05 });
  gableRoof(A, { X, Z, XI, EAVE, RIDGE, ov: 0.4, gableMat: "stone", roofMat: "slate" });
  for (const sx of [-1, 1]) for (const sz of [-1, 1]) A.wedge([sx * (X + 0.25), 1.0, sz * (Z - 0.6)], [0.6, 2.0, 0.6], "stone_dressed", { r: [0, sx > 0 ? 90 : -90, 0], top: 0.3 });   // buttresses
  // bellcote over the ridge with its bell, a cross on top
  const yb = RIDGE - 0.6;
  for (const sx of [-1, 1]) A.box([sx * 0.45, yb, -0.3], [sx * 0.8, yb + 2.0, 0.3], "stone_dressed", { bv: 0.012 });
  A.box([-0.9, yb, -0.35], [0.9, yb + 0.5, 0.35], "stone_dressed", { bv: 0.012 });
  A.arch([0, yb + 2.0 + 0.3, 0], [1.6, 0.6, 0.6], "stone_dressed", { th: 0.35, fill: true });
  A.prism(2, -0.34, 0.34, [[-0.95, yb + 2.58], [0.95, yb + 2.58], [0, yb + 3.3]], "slate");
  A.cone([0, yb + 1.25, 0], [0.55, 0.55, 0.55], "brass", { top: 0.45, k: 0 });
  A.box([-0.04, yb + 3.2, -0.04], [0.04, yb + 4.1, 0.04], "iron", { k: 0 });
  A.box([-0.28, yb + 3.75, -0.035], [0.28, yb + 3.83, 0.035], "iron", { k: 0 });
  // chapel: altar and an oak rail; lodge: hearth
  A.beginFurniture("Altar"); A.box([XI - 0.75, GF, -0.8], [XI - 0.05, GF + 1.0, 0.8], "stone_dressed", { bv: 0.01 }); A.box([XI - 0.85, GF + 1.0, -0.95], [XI, GF + 1.08, 0.95], "linen"); A.endFurniture();
  A.beginFurniture("Altar candlesticks"); for (const z of [-0.6, 0.6]) A.cyl([XI - 0.35, GF + 1.25, z], [0.06, 0.34, 0.06], "brass", { n: 8 }); A.box([XI - 0.4, GF + 1.08, -0.65], [XI - 0.3, GF + 1.42, 0.65], "brass", { v: 0 }); A.endFurniture(false);
  fireplace(A, { axis: "z", d: -XI, s: 1, a: -1.2, y: GF, top: EAVE - 0.2, w: 1.5 });
  chimney(A, { x: -X + 0.2, z: -1.2, y0: EAVE - 0.4, y1: RIDGE - 0.6, w: 0.6, d: 0.8 });
  setNav(A, {
    effect: "block",
    rooms: [room("lodge", [-XI, GF, -ZI], [-P - PW, EAVE - 0.2, ZI]), room("chapel", [P + PW, GF, -ZI], [XI, EAVE - 0.2, ZI])],
    doors: [{ a: "lodge", b: "outside", p: [-P - PW / 2, GF, 0] }, { a: "chapel", b: "outside", p: [P + PW / 2, GF, 0] }, { a: "chapel", b: "outside", p: [3.9, GF, q4(Z - T / 2)] }],
    entrances: [{ p: [-0.9, 0, 0], dir: [-1, 0, 0], room: "lodge" }, { p: [0.9, 0, 0], dir: [1, 0, 0], room: "chapel" }, { p: [3.9, 0, Z + 1.0], dir: [0, 0, -1], room: "chapel" }],
    corners: [...corners("lodge", -XI, -P - PW, -ZI, ZI, GF), ...corners("chapel", P + PW, XI, -ZI, ZI, GF)],
    firingPoints: [fire([-3.9, GF, ZI - 0.6], 0, "window"), fire([-3.9, GF, -ZI + 0.6], 180, "window"), fire([-XI + 0.6, GF, 0], 270, "window"), fire([XI - 0.6, GF, 0], 90, "window"), fire([3.9, GF, -ZI + 0.6], 180, "window")],
    cover: [cover([X + 0.6, 0, 1.6], 270, "high", ["right"]), cover([-X - 0.6, 0, 1.6], 90, "high", ["left"]), cover([-3.0, 0, -Z - 0.6], 0, "high", ["left", "right"])],
  });
  setTour(A, { pos: [0, 0, Z + 6], yaw: 0 }, [
    { goto: [0, Z + 1.0], note: "approach the carriage arch" },
    { goto: [0, 0.3], note: "under the vault" }, { look: [0, ys + 1.0, -Z], hold: 0.4 }, { shot: "b3_gatehouse_a_passage" },
    { goto: [-0.95, 0.3] }, { interact: "lodge_door", aim: [-P - 0.26, GF + 1.1, 0.25], expectOpen: true },
    { goto: [-3.6, 0.3], expectY: GF, note: "into the lodge" }, { look: [-XI, GF + 1.0, -1.0], hold: 0.4 }, { shot: "b3_gatehouse_b_lodge" },
    { goto: [-0.95, 0.3] }, { goto: [0.95, 0.3] }, { interact: "chapel_door", aim: [P + 0.26, GF + 1.1, -0.25], expectOpen: true },
    { goto: [3.8, 0.3], expectY: GF, note: "into the chapel" }, { look: [XI, GF + 1.2, 0], hold: 0.4 }, { shot: "b3_gatehouse_c_chapel" },
    { goto: [3.9, ZI - 1.3] }, { interact: "chapel_front_door", aim: [4.15, GF + 1.1, ZI + 0.05], expectOpen: true },
    { goto: [3.9, Z + 1.6], note: "leave by the chapel front door" },
  ]);
  weather(A, { keep: [plinth] });
  interiorTrim(A, { levels: [GF] });   // GX2
  A.meta.companions = [comp("tree_ash", -10, -5, 30), comp("shrub_hazel", 8.6, 3.2), comp("nat_flowers_foxglove", -4.5, 4.9), comp("nat_fern_clump", 8.2, -2.5)];
  return A;
}

// ============================================================================ 7. mill house with the waterwheel annex
export function millHouse() {
  const X = 7.0, Z = 4.8, WX = X + 1.25, WY = 2.35, WR = 2.1, WZ = -0.4;
  const A = meta("vil_mill_house", "Mill house with waterwheel", "Houses",
    "Stone water mill and miller's house, 14 x 9.6 m under a slate roof, with a lean-to wheelhouse annex on the east gable sheltering an oak breast-shot waterwheel (4.2 m, sixteen spokes, buckets) in its stone race. Mill room with the millstones, store; two bedrooms over; two stairs; ivy.",
    2, [20.4, 11], { ivy: "auto" });
  mats(A);
  house(A, {
    X, Z, T: 0.55, h: 3.2, n: 2, shot: "b3_mill",
    style: () => ({ m: "stone" }), wd: () => ({ w: 1.0, h: 1.35, sill: 0.9 }),
    win: (f) => (f === 0 ? { front: [-5.2, -0.4, 2.1], rear: [-5.0, -1.5], west: [2.75] } : { front: [-5.2, -1.9, 1.9, 5.2], rear: [-5.0, -1.5, 5.0], west: [2.75], east: [3.6] }),
    parts: (f) => (f === 0 ? [0.4] : [0]), names: (f, r) => (f === 0 ? ["mill_room", "store"][r] : `bedroom_${r + 1}`), hall: 0, rdx: 2.1,
    roof: { type: "gable", rise: 4.8, m: "slate", gable: "stone" },
    dress: (A, k) => {
      masonry(A, k, { sc: false });
      chimney(A, { x: -X + 0.17, z: -1.0, y0: k.EAVE - 0.4, y1: k.RIDGE + 0.9, w: 0.55, d: 0.8 });
      // race and wheelhouse: inner and outer race walls, water, the annex wall with oak posts, a tiled lean-to roof
      A.box([X + 0.02, -0.4, -3.3], [X + 0.25, 0.9, 2.5], "stone_dressed", { bv: 0.012 });
      A.box([X + 2.25, -0.4, -3.3], [X + 2.75, 1.2, 2.5], "stone", {});
      A.box([X + 2.2, 1.2, -3.35], [X + 2.8, 1.32, 2.55], "stone_dressed", { bv: 0.012 });
      A.box([X + 0.25, -0.4, -3.3], [X + 2.25, 0.34, 2.5], "water", { k: 0 });
      A.box([X + 0.25, -0.4, -3.3], [X + 2.25, 0.2, 2.5], "stone", { v: 0 });
      for (const z of [-3.0, -0.4, 2.2]) A.box([X + 2.38, 1.32, z - 0.12], [X + 2.62, 3.25, z + 0.12], "oak_y", { bv: 0.01 });
      A.box([X + 2.32, 3.2, -3.4], [X + 2.68, 3.42, 2.6], "oak_x", { bv: 0.01 });
      const ry0 = 5.35, ry1 = 3.32, run = 3.1, ang = (Math.atan2(ry0 - ry1, run) * 180) / Math.PI;
      A.obox([X + run / 2 - 0.05, (ry0 + ry1) / 2 + 0.07, -0.4], [Math.hypot(run, ry0 - ry1) + 0.1, 0.12, 6.4], [0, 0, -ang], "tile");
      // the wheel: hub, two sets of spokes, two rims, buckets; one invisible collider
      A.cyl([X - 0.2 + (WX - X + 0.2 + 0.6) / 2, WY, WZ], [0.26, WX - X + 0.8, 0.26], "oak_y", { r: [0, 0, 90], k: 0 });
      A.cyl([WX, WY, WZ], [0.6, 1.0, 0.6], "oak_y", { r: [0, 0, 90], k: 0, n: 8 });
      for (const sx of [-0.4, 0.4]) {
        for (let i = 0; i < 8; i++) A.obox([WX + sx, WY, WZ], [0.1, 2 * WR - 0.1, 0.13], [i * 22.5, 0, 0], "oak_y", { k: 0 });
        for (let i = 0; i < 24; i++) { const a = (i * 15 * Math.PI) / 180; A.obox([WX + sx, WY + WR * Math.cos(a), WZ + WR * Math.sin(a)], [0.09, 0.16, 2 * Math.PI * WR / 24 + 0.03], [i * 15, 0, 0], "oak_x", { k: 0 }); }
      }
      for (let i = 0; i < 24; i++) { const a = ((i * 15 + 7.5) * Math.PI) / 180, r = WR - 0.16; A.obox([WX, WY + r * Math.cos(a), WZ + r * Math.sin(a)], [0.88, 0.32, 0.04], [i * 15 + 7.5, 0, 0], "plank_x", { k: 0 }); }
      A.cyl([WX, WY, WZ], [2 * WR, 0.95, 2 * WR], "oak_x", { r: [0, 0, 90], v: 0 });
      // millstones in the mill room
      A.beginFurniture("Millstones");
      A.box([-3.6, GF, 1.6], [-2.3, GF + 0.45, 2.9], "oak_x");
      A.cyl([-2.95, GF + 0.55, 2.25], [1.2, 0.2, 1.2], "stone_dressed");
      A.cyl([-2.95, GF + 0.76, 2.25], [1.2, 0.2, 1.2], "stone_dressed");
      A.cone([-2.95, GF + 1.15, 2.25], [0.7, 0.55, 0.7], "plank_y", { top: 0.3, n: 4 });
      A.box([-3.6, GF, 1.6], [-2.3, GF + 1.4, 2.9], "oak_x", { v: 0 });
      A.endFurniture();
      fireplace(A, { axis: "x", d: k.zA + 0.15, s: 1, a: -3.0, y: k.yAt(1), top: k.EAVE - 0.2, w: 1.2 });
    },
    cover: [cover([-X - 0.6, 0, 2.2], 90, "high", ["left"]), cover([-X - 0.6, 0, -2.2], 90, "high", ["right"]), cover([3.0, 0, -Z - 0.6], 0, "high", ["left", "right"])],
  });
  A.meta.footprint = [2 * (X + 3.2), 11];
  A.meta.companions = [comp("tree_ash", -11.5, -3), comp("shrub_hazel", -10, 5.2), comp("nat_fern_clump", X + 3.8, 3.8), comp("nat_nettle_clump", X + 3.8, -4.2)];
  return A;
}

// ============================================================================ 8. terraced townhouse pair
export function terracePair() {
  const X = 5.6, Z = 4.6;
  const A = meta("vil_terrace_pair", "Terraced townhouse pair", "Houses",
    "Pair of narrow three-storey terraced town houses, 11.2 x 9.2 m together: one rendered, one brick, with stone quoins and string courses, shuttered windows and a slate roof split by a party-wall upstand carrying the shared chimney stack. Each house has its own front door, a room on every floor and its own stair; the rear corridors are knocked through the party wall on every floor.",
    3, [12.2, 10.6], { ivy: [{ face: "-x", a0: -3.0, a1: 3.0, y0: 0.4, y1: 6.0 }] });
  mats(A);
  house(A, {
    X, Z, T: 0.45, h: 3.0, n: 3, shot: "b3_terrace", party: [0],
    style: () => ({ m: "render", split: [{ a0: -X, a1: 0, m: "render" }, { a0: 0, a1: X, m: "brick" }], west: "render", east: "brick" }),
    wd: () => ({ w: 0.9, h: 1.5, sill: 0.8 }),
    win: (f) => (f === 0 ? { front: [-3.85, 3.85], rear: [-3.85, 3.85] } : { front: [-3.85, -1.25, 1.25, 3.85], rear: [-3.85, -1.25, 1.25, 3.85] }),
    parts: () => [0], names: (f, r) => `${["west", "east"][r]}${f}`, hall: 0, rdx: -1.25, doorW: 1.0,
    doors: [{ id: "east_front_door", side: "front", c: 1.25, w: 1.0, h: 2.25, room: "east0" }],
    roof: { type: "gable", rise: 4.2, m: "slate", gable: "stone" },
    dress: (A, k) => {
      masonry(A, k, { sc: true });
      const { EAVE, RIDGE } = k;
      A.prism(0, -0.16, 0.16, [[-Z - 0.36, EAVE - 0.1], [Z + 0.36, EAVE - 0.1], [0, RIDGE + 0.42]], "stone");
      chimney(A, { x: 0, z: -0.3, y0: EAVE, y1: RIDGE + 1.4, w: 1.1, d: 0.6, m: "brick", pots: 4 });
      for (const s of [-1, 1]) fireplace(A, { axis: "z", d: s * 0.075, s, a: 2.4, y: GF, top: k.yAt(1) - 0.2, w: 1.1 });
    },
  });
  A.meta.companions = [comp("nat_flowers_daisy", -3.2, 5.4), comp("nat_flowers_poppy", 3.2, 5.4)];
  return A;
}

// ============================================================================ 9. farm cottage with barn and dovecote
export function cottageBarn() {
  const X = 10, Z = 3.9;
  const A = meta("vil_cottage_barn", "Farm cottage with barn and dovecote", "Houses",
    "Norman farm range, 20 x 7.8 m under one tiled roof: a rendered cottage (kitchen with hearth, living room, ceiled) joined by a doorway to a rubble-stone barn open to the rafters with an earth floor and double cart doors, and a round stone dovecote turret with pigeon holes and a conical roof at the barn's rear corner.",
    1, [24.8, 12.6], { ivy: [{ face: "+x", a0: -2.0, a1: 2.0, y0: 0.4, y1: 3.4 }] });
  mats(A);
  const T = 0.5, tx = X + 0.9, tz = -Z - 0.9, tr = 1.5;
  house(A, {
    X, Z, T, h: 3.6, n: 1, shot: "b3_cottage", ceilingTo: 0,
    floorMats: [[-X + T, 0, "terracotta"], [0, X - T, "earth"]],
    style: () => ({ m: "stone", split: [{ a0: -X, a1: 0, m: "render" }, { a0: 0, a1: X, m: "stone" }], west: "render", east: "stone" }),
    wd: () => ({ w: 0.9, h: 1.1, sill: 0.9 }),
    win: () => ({ front: [-7.6, -0.9, { c: 1.8, w: 0.5, h: 0.7, sill: 1.7, shutters: false }, { c: 8.3, w: 0.5, h: 0.7, sill: 1.7, shutters: false }],
      rear: [-5.4, -2.3, { c: 3.0, w: 0.5, h: 0.7, sill: 1.7, shutters: false }, { c: 6.6, w: 0.5, h: 0.7, sill: 1.7, shutters: false }], west: [0] }),
    parts: () => [-4.6, 0], names: (f, r) => ["kitchen", "living_room", "barn"][r], hall: 1, rdx: -7.05,
    doors: [{ id: "cart_doors", side: "front", c: 4.75, w: 2.8, h: 2.9, double: true, room: "barn" }],
    roof: { type: "gable", rise: 4.3, m: "tile", gable: "stone" },
    dress: (A, k) => {
      const { EAVE, RIDGE, ZI } = k;
      quoins(A, X, Z, GF - 0.02, EAVE);
      A.prism(0, -0.08, 0.08, [[-ZI, EAVE], [ZI, EAVE], [0, RIDGE - 0.2]], "stone");   // gable wall between cottage loft and barn
      // hearth on the kitchen side of the partition, not the gable: behind a gable bay it blocked the breach (--selftest-damage)
      fireplace(A, { axis: "z", d: -4.675, s: -1, a: 1.0, y: GF, top: EAVE - 0.2, w: 1.7 });
      chimney(A, { x: -4.6, z: 1.0, y0: EAVE - 0.2, y1: RIDGE + 0.9, w: 0.6, d: 0.8 });
      for (let x = 0.5; x < X - T - 0.2; x += 1.0) A.box([x - 0.08, EAVE - 0.3, -ZI], [x + 0.08, EAVE - 0.12, ZI], "oak_z", { k: 0 });   // barn tie beams
      turret(A, { x: tx, z: tz, y0: 0, y1: EAVE + 3.2, r: tr, spire: 3.4, roof: "tile", m: "stone" });
      for (let i = 0; i < 14; i++) {   // pigeon holes and their landing ledge
        const a = (i / 14) * Math.PI * 2, d = [Math.cos(a), Math.sin(a)], yaw = (Math.atan2(d[0], d[1]) * 180) / Math.PI;
        A.obox([tx + d[0] * (tr + 0.0), EAVE + 2.2, tz + d[1] * (tr + 0.0)], [0.16, 0.2, 0.06], [0, yaw, 0], "soot", { k: 0 });
      }
      A.cyl([tx, EAVE + 2.02, tz], [2 * tr + 0.16, 0.08, 2 * tr + 0.16], "stone_dressed", { k: 0 });
    },
    cover: [cover([-X - 0.6, 0, 1.8], 90, "high", ["left"]), cover([-X - 0.6, 0, -1.8], 90, "high", ["right"]), cover([X + 0.6, 0, 1.8], 270, "high", ["right"]), cover([-5.0, 0, -Z - 0.6], 0, "high", ["left", "right"])],
  });
  A.meta.companions = [comp("tree_apple", -13.5, 5.5), comp("nat_nettle_clump", X + 1.2, 5.6), comp("shrub_bramble", 13.2, 2.2), comp("nat_drygrass_tall", 6, -6.5)];
  return A;
}

// ============================================================================ 10. rectory with a hip roof and dormers
export function rectory() {
  const X = 7.5, Z = 5.5;
  const A = meta("vil_rectory", "Rectory (presbytere)", "Houses",
    "Village presbytery, 15 x 11 m: a symmetrical rendered front with dressed-stone quoins, string course and cornice under a slate hip roof with three dormers and two chimneys, a stone door case with pilasters and a pediment, shuttered windows. Study, hall and dining room; three rooms above; two stairs; ivy.",
    2, [16.4, 13.2], { ivy: "auto" });
  mats(A);
  house(A, {
    X, Z, T: 0.55, h: 3.3, n: 2, shot: "b3_rectory",
    style: () => ({ m: "render" }), wd: () => ({ w: 1.1, h: 1.6, sill: 0.85 }),
    win: (f) => (f === 0 ? { front: [-5.65, -2.85, 2.85, 5.65], rear: [-5.0, -2.5, 2.5, 5.0], east: [2.85], west: [2.85] } : { front: [-5.65, -2.85, 0, 2.85, 5.65], rear: [-5.0, -2.5, 0, 2.5, 5.0], east: [2.85], west: [2.85] }),
    parts: () => [-1.4, 1.4], names: (f, r) => (f === 0 ? ["study", "hall", "dining"][r] : `room_${r + 1}`), hall: 1, rdx: 0,
    roof: { type: "hip", rise: 4.0, m: "slate" },
    dress: (A, k) => {
      const { EAVE, yAt } = k, fr = face("front", X, Z);
      masonry(A, k, { cor: true });
      for (const a of [-0.95, 0.95]) pilaster(A, fr, a, GF, GF + 2.55, { w: 0.3 });
      fbox(A, fr, -1.25, 1.25, GF + 2.55, GF + 2.75, -0.01, 0.14, "stone_dressed", { bv: 0.012 });
      A.prism(2, Z - 0.01, Z + 0.13, [[-1.3, GF + 2.75], [1.3, GF + 2.75], [0, GF + 3.3]], "stone_dressed");
      const ye = EAVE + 0.42 - 0.45 * (4.0 / (Z + 0.05)), pitch = 4.0 / (Z + 0.05), surf = (z) => ye + (Z + 0.5 - Math.abs(z)) * pitch;
      for (const [x, s] of [[-2.85, 1], [2.85, 1], [0, -1]]) dormer(A, { x, z: s * (Z - 0.55), y: surf(Z - 0.55) - 0.06, w: 1.1, h: 1.15, depth: 1.6, s, roof: "slate" });
      for (const sx of [-1, 1]) chimney(A, { x: sx * 3.6, z: -0.6, y0: EAVE, y1: EAVE + 0.42 + 4.0 + 1.0, w: 0.8, d: 0.6, m: "stone_dressed" });
      for (const x of [-3.7, 3.7]) fireplace(A, { axis: "x", d: k.zA + 0.15, s: 1, a: x, y: GF, top: yAt(1) - 0.2, w: 1.4 });
    },
  });
  A.meta.companions = [comp("tree_oak", -12.5, -6.5), comp("shrub_hazel", 9.8, 6.2), comp("shrub_hazel", -9.8, 6.2, 120), comp("nat_flowers_cornflower", -3.2, 6.8), comp("nat_flowers_daisy", 3.2, 6.8)];
  return A;
}

export function chateauAssets() {
  return [chateauGrand(), longhouse(), townhouseJettied(), villaDeauville(), manorTurret(), cornerShop(), gatehouse(), millHouse(), terracePair(), cottageBarn(), rectory()];
}
