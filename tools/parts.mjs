// Reusable architectural and furniture components for Norman buildings.
import { P, wbox, cutRect, swingSign } from "./kit.mjs";

export function stdMaterials(A) {
  // albedo values are linear-space
  A.mat("stone", [0.40, 0.36, 0.285], 0.88, "stone");
  A.mat("stone_dressed", [0.52, 0.48, 0.38], 0.82, "stone");
  A.mat("stone_plinth", [0.27, 0.255, 0.22], 0.9, "stone");
  A.mat("hearthstone", [0.22, 0.21, 0.19], 0.8, "stone");
  A.mat("plaster", [0.64, 0.61, 0.54], 0.93, "plaster");
  A.mat("plaster_smoke", [0.50, 0.46, 0.39], 0.93, "plaster");
  A.mat("soot", [0.025, 0.022, 0.02], 0.97, "plain");
  A.mat("oak_x", [0.15, 0.095, 0.055], 0.72, "wood", { grain: 0 });
  A.mat("oak_y", [0.15, 0.095, 0.055], 0.72, "wood", { grain: 1 });
  A.mat("oak_z", [0.15, 0.095, 0.055], 0.72, "wood", { grain: 2 });
  A.mat("floor_x", [0.24, 0.16, 0.095], 0.62, "wood", { grain: 0 });
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
  A.mat("terracotta", [0.36, 0.14, 0.075], 0.72, "tile");
  A.mat("iron", [0.035, 0.035, 0.035], 0.55, "metal", { metallic: 0.85 });
  A.mat("cast_iron", [0.025, 0.025, 0.025], 0.62, "metal", { metallic: 0.75 });
  A.mat("zinc", [0.33, 0.34, 0.35], 0.5, "metal", { metallic: 0.9 });
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
  A.mat("steel_plate", [0.075, 0.085, 0.07], 0.55, "metal", { metallic: 0.55 });
  A.mat("steel_bare", [0.30, 0.30, 0.31], 0.4, "metal", { metallic: 1.0 });
  A.mat("turf", [0.085, 0.12, 0.04], 0.98, "plain");
  A.mat("earth", [0.12, 0.09, 0.06], 1.0, "plain");
  A.mat("log_x", [0.16, 0.12, 0.08], 0.9, "wood", { grain: 0 });
  A.mat("log_z", [0.16, 0.12, 0.08], 0.9, "wood", { grain: 2 });
  A.mat("plank_y", [0.26, 0.19, 0.12], 0.8, "wood", { grain: 1 });
  A.mat("plank_x", [0.26, 0.19, 0.12], 0.8, "wood", { grain: 0 });
  A.mat("plank_z", [0.26, 0.19, 0.12], 0.8, "wood", { grain: 2 });
  A.mat("render", [0.58, 0.53, 0.43], 0.92, "plaster");
  A.mat("crate_olive", [0.11, 0.12, 0.06], 0.7, "painted", { grain: 0 });
  A.mat("canvas", [0.30, 0.28, 0.20], 0.95, "fabric");
  A.mat("paper", [0.68, 0.64, 0.52], 0.9, "plain");
  A.mat("blackboard", [0.04, 0.05, 0.045], 0.8, "plain");
  A.mat("sacking", [0.40, 0.32, 0.20], 0.95, "fabric");
}

// --------------------------------------------------------------------------- walls
// A two-layer wall: rubble stone outside, lime plaster inside, with real openings.
// out: +1/-1 = direction of the exterior along the wall's depth axis.
export function wall(A, { axis, outer, inner, aOut, aIn, y0, y1, holes, stone = "stone", plaster = "plaster", pt = 0.03 }) {
  const out = Math.sign(outer - inner);
  const mid = inner + out * pt;
  for (const [a0, a1, ya, yb] of cutRect(aOut[0], aOut[1], y0, y1, holes)) wbox(A, axis, a0, a1, ya, yb, mid, outer, stone);
  for (const [a0, a1, ya, yb] of cutRect(aIn[0], aIn[1], y0, y1, holes)) wbox(A, axis, a0, a1, ya, yb, inner, mid, plaster);
}
export function partition(A, { axis, d0, d1, a0, a1, y0, y1, holes, m = "plaster" }) {
  for (const [p0, p1, ya, yb] of cutRect(a0, a1, y0, y1, holes)) wbox(A, axis, p0, p1, ya, yb, d0, d1, m);
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

  // ---- exterior shutters (persiennes pleines), open outward against the facade
  if (o.shutters !== false) {
    const sl = [];
    const sy0 = sill - 0.03, sy1 = top + 0.03;
    const sd0 = outer + out * 0.005, sd1 = outer + out * 0.04;
    for (const side of [0, 1]) {
      const L = { parts: [] };
      const s0 = side === 0 ? a0 - 0.03 : c, s1 = side === 0 ? c : a1 + 0.03;
      const T = { into: L.parts, k: 0 };
      // vertical boards
      wbox(A, axis, s0, s1, sy0, sy1, sd0, sd1, "shutter_paint", T);
      // battens and Z-brace on the inner face
      for (const y of [sy0 + 0.18, sy1 - 0.18]) wbox(A, axis, s0 + 0.03, s1 - 0.03, y - 0.05, y + 0.05, sd0 - out * 0.025, sd0, "shutter_paint", T);
      // strap hinges (exterior face)
      const hingeA = side === 0 ? a0 - 0.03 : a1 + 0.03;
      for (const y of [sy0 + 0.2, sy1 - 0.2]) {
        const e0 = hingeA, e1 = hingeA + (side === 0 ? 0.3 : -0.3);
        wbox(A, axis, e0, e1, y - 0.02, y + 0.02, sd1, sd1 + out * 0.006, "iron", T);
      }
      wbox(A, axis, s0, s1, sy0, sy1, sd0, sd1, "shutter_paint", { into: L.parts, v: 0 });
      const hinge = P(axis, hingeA, 0, outer + out * 0.045);
      const mid = P(axis, (s0 + s1) / 2, 0, (sd0 + sd1) / 2);
      L.hinge = hinge;
      L.sign = swingSign(axis, mid, hinge, out);
      sl.push(L);
    }
    A.interactables.push({
      id: id + "_shutters", kind: "shutters", label: "shutters", sound: "shutter", axis: [0, 1, 0], openAngle: 172, duration: 1.0,
      defaultOpen: o.shuttersOpen !== false, leaves: sl,
      aperture: apBox(axis, a0, a1, sill, top, outer - out * 0.12, outer + out * 0.06),
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
