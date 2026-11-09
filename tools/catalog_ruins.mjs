// Ruined buildings and rubble (Visual & Physics Overhaul V3, docs/plan-visual-physics-overhaul.md items 0 / 0a,
// docs/assets-mpu.md). Six walkable ruins (shelled two-storey house, gutted farmhouse, half-collapsed shop terrace,
// burnt-out barn, cottage corner, bombed townhouse facade) and ten rubble pieces (brick / stone piles S M L, a beam and
// tile heap, a collapsed wall section, a snappable spill strip, a scattered debris patch).
// Built with the house kit: two-layer walls through parts.wall() (so every standing bay is a damage panel; the rest is
// picked up by the loader's auto panels), ragged tops, k:0 rubble chunks on walkable stepped heaps, setNav / setTour.
// Shimmer rules (plan V1 facade item): no coplanar faces between materials. Walls along X own the corners and their
// plaster skin is inset to the inner span; end walls stop at the inner faces (as in catalog_village2 shell(), unlike its
// raggedWall). Patches, sills and noses are embedded into the surface they sit on rather than laid on it.
// Regenerate with `node tools/build_mpu_assets.mjs` (never run build_assets.mjs).
import { Asset, wbox } from "./kit.mjs";
import { stdMaterials, wall, door, stairs, railing } from "./parts.mjs";
import { rng, fire, cover, room, corners, setNav, setTour, r3 } from "./catalog_atlantic.mjs";
import { debrisDetail } from "./defence_detail.mjs";

const PT = 0.03;
const D2R = Math.PI / 180;

function ruinMaterials(A) {
  stdMaterials(A);
  A.mat("ash", [0.15, 0.14, 0.13], 0.98, "plain");
  A.mat("charred", [0.04, 0.034, 0.03], 0.92, "wood", { grain: 0 });
  A.mat("wallpaper", [0.46, 0.4, 0.3], 0.9, "painted", { grain: 1 });
  A.mat("brick_rubble", [0.2, 0.115, 0.085], 0.97, "stone");
  A.mat("shop_paint", [0.1, 0.17, 0.2], 0.6, "painted", { grain: 0 });
}

// ============================================================================ rubble heaps
// Material mixes for the loose chunks: [material, kind, weight]
const MIX = {
  stone: [["stone", "block", 7], ["stone_dressed", "block", 2], ["rubble_core", "dust", 3], ["plaster_smoke", "flake", 1]],
  brick: [["brick", "brick", 8], ["brick", "block", 2], ["brick_rubble", "dust", 3], ["plaster_smoke", "flake", 1], ["stone", "block", 1]],
  house: [["stone", "block", 5], ["stone_dressed", "block", 1], ["plaster", "flake", 1], ["plaster_smoke", "flake", 1], ["rubble_core", "dust", 3], ["slate", "tile", 2], ["oak_x", "beam", 1]],
  roof: [["terracotta", "tile", 5], ["slate", "tile", 3], ["charred", "beam", 2], ["oak_x", "beam", 1], ["rubble_core", "dust", 2], ["plaster", "flake", 1]],
  burnt: [["charred", "beam", 4], ["ash", "dust", 4], ["stone", "block", 3], ["soot", "dust", 1], ["slate", "tile", 1]],
  town: [["stone_dressed", "block", 5], ["stone", "block", 2], ["brick", "brick", 2], ["plaster", "flake", 2], ["rubble_core", "dust", 3], ["slate", "tile", 1], ["oak_x", "beam", 1]],
};
function pick(R, mix) {
  const tot = mix.reduce((s, m) => s + m[2], 0);
  let u = R() * tot;
  for (const m of mix) { u -= m[2]; if (u <= 0) return m; }
  return mix[mix.length - 1];
}
// a rotated chunk: rounded (rad > 0) or plain box, never colliding
function chunk(A, c, s, r, m, rad = 0) {
  const p = { t: rad > 0 ? "r" : "b", c: c.map(r3), s: s.map(r3), r: r.map(r3), m, k: 0 };
  if (rad > 0) p.rad = r3(Math.min(rad, ...s.map((x) => x / 2)));
  A.parts.push(p);
  return p;
}
const clipBox = (mn, mx, clip) => {
  if (!clip) return [mn, mx];
  const a = [Math.max(mn[0], clip[0]), mn[1], Math.max(mn[2], clip[2])], b = [Math.min(mx[0], clip[1]), mx[1], Math.min(mx[2], clip[3])];
  return a[0] < b[0] - 0.05 && a[2] < b[2] - 0.05 ? [a, b] : null;
};
// A walkable rubble heap: a stepped mound of rounded tiers (visible rubble core, solid; each tier a cross of two
// rounded boxes, steps <= `step` with rounded noses, so the character walks up it like a stair) covered by loose
// k:0 chunks resting on the tier tops. Base half-extents rx / rz at height y, crest h above y; ft = crest size as a
// fraction of the base. clip: [x0, x1, z0, z1] keeps everything inside (heaps against walls).
function heap(A, R, { cx, cz, rx, rz, h, y = 0, step = 0.25, ft = 0.16, mix = MIX.stone, clip = null, density = 7, collide = true, core = "rubble_core", chunks = true }) {
  const n = Math.max(1, Math.ceil(h / step - 0.05)), tiers = [];
  for (let k = 1; k <= n; k++) {
    const t = (h * k) / n, f = 1 - ((1 - ft) * t) / h + (1 - ft) / n * 0.5;   // nose of each step near the cone line
    const ex = rx * Math.min(1, f), ez = rz * Math.min(1, f);
    for (const [hx, hz, top] of [[ex, ez * 0.72, t], [ex * 0.72, ez, t - 0.02]]) {
      // free-standing heaps: each tier box a little off-centre and turned, so the steps don't read as a ziggurat
      // jitter and yaw move a tier corner by at most a quarter of the tier spacing, so each tier stays nested in the one
      // below (bigger offsets on wide heaps made 0.45 m double steps the character cannot climb); same R() draws as before
      const lim = (0.72 * Math.min(rx, rz) * (1 - ft)) / n / 4;   // 0.72: the narrow arm of each cross
      const jx = clip ? 0 : (R() - 0.5) * Math.min(0.08 * hx, lim), jz = clip ? 0 : (R() - 0.5) * Math.min(0.08 * hz, lim);
      const yaw = clip ? 0 : (R() - 0.5) * Math.min(14, (2 * lim / Math.max(hx, hz)) / D2R);
      const bb = clipBox([cx + jx - hx, y - 0.15, cz + jz - hz], [cx + jx + hx, y + top, cz + jz + hz], clip);
      if (!bb) continue;
      const rad = Math.min(0.2, step * 0.8), c = [0, 1, 2].map((i) => (bb[0][i] + bb[1][i]) / 2), sz = [0, 1, 2].map((i) => bb[1][i] - bb[0][i]);
      const p = { t: "r", c: c.map(r3), s: sz.map(r3), rad: r3(Math.min(rad, ...sz.map((q) => q / 2))), m: core };
      if (yaw) p.r = [0, r3(yaw), 0];
      if (!collide) p.k = 0;
      A.parts.push(p);
      const m = Math.abs(Math.sin(yaw * D2R)) * Math.max(hx, hz);   // turned: trust only the inner part
      tiers.push({ x0: bb[0][0] + m, x1: bb[1][0] - m, z0: bb[0][2] + m, z1: bb[1][2] - m, top: y + top });
    }
  }
  const H = (x, z) => { let v = y; for (const q of tiers) if (x > q.x0 + 0.05 && x < q.x1 - 0.05 && z > q.z0 + 0.05 && z < q.z1 - 0.05) v = Math.max(v, q.top); return v; };
  if (chunks) {
    const cnt = Math.max(10, Math.min(240, Math.round(density * Math.PI * rx * rz)));
    for (let i = 0; i < cnt; i++) {
      let x, z;
      if (tiers.length && R() < 0.45) {   // on a step nose: breaks up the rounded tier edges
        const q = tiers[Math.floor(R() * tiers.length)], u = R(), side = Math.floor(R() * 4);
        x = side < 2 ? q.x0 + u * (q.x1 - q.x0) : side === 2 ? q.x0 + 0.06 : q.x1 - 0.06;
        z = side >= 2 ? q.z0 + u * (q.z1 - q.z0) : side === 0 ? q.z0 + 0.06 : q.z1 - 0.06;
      } else {
        const ang = R() * Math.PI * 2, rr = Math.sqrt(R()) * 1.05;
        x = cx + Math.cos(ang) * rx * rr; z = cz + Math.sin(ang) * rz * rr;
      }
      if (clip && (x < clip[0] + 0.15 || x > clip[1] - 0.15 || z < clip[2] + 0.15 || z > clip[3] - 0.15)) continue;
      scatter(A, R, x, H(x, z), z, pick(R, mix));
    }
  }
  return { H, top: y + h };
}
// one loose piece of debris resting at (x, base, z)
function scatter(A, R, x, base, z, [m, kind]) {
  const yaw = R() * 360, tilt = () => (R() - 0.5) * 50;
  if (kind === "block") {
    const s = [0.18 + R() * 0.32, 0.12 + R() * 0.22, 0.15 + R() * 0.28];
    chunk(A, [x, base + s[1] * 0.22, z], s, [tilt(), yaw, tilt()], m, R() < 0.4 ? 0.04 : 0);
  } else if (kind === "brick") {   // a clump of bricks still mortared together, or a lone brick
    const lone = R() < 0.35, s = lone ? [0.22, 0.065, 0.105] : [0.24 + R() * 0.3, 0.07 + R() * 0.14, 0.11 + R() * 0.2];
    chunk(A, [x, base + s[1] * 0.25, z], s, [tilt(), yaw, tilt()], m);
  } else if (kind === "flake") {
    const s = [0.2 + R() * 0.3, 0.025 + R() * 0.02, 0.15 + R() * 0.25];
    chunk(A, [x, base + 0.02, z], s, [tilt() * 0.6, yaw, tilt() * 0.6], m);
  } else if (kind === "tile") {   // roof tiles / slates, sometimes a small stack
    const nT = R() < 0.3 ? 3 : 1;
    for (let k = 0; k < nT; k++) chunk(A, [x + (R() - 0.5) * 0.1, base + 0.015 + k * 0.022, z + (R() - 0.5) * 0.1], m === "slate" ? [0.3, 0.014, 0.2] : [0.26, 0.018, 0.17], [tilt() * 0.5, yaw + k * 17, tilt() * 0.5], m);
  } else if (kind === "beam") {
    const w = 0.1 + R() * 0.08, L = 0.6 + R() * 1.6;
    chunk(A, [x, base + w * 0.5, z], [w, w * 1.15, L], [(R() - 0.5) * 24, yaw, (R() - 0.5) * 10], m);
  } else {   // dust / small fill
    const s = [0.3 + R() * 0.45, 0.05 + R() * 0.05, 0.25 + R() * 0.4];
    chunk(A, [x, base + 0.01, z], s, [tilt() * 0.2, yaw, tilt() * 0.2], m, 0.025);
  }
}
// scattered debris on a flat area (floors, ground around a ruin)
function strew(A, R, { x0, x1, z0, z1, y, n, mix }) {
  for (let i = 0; i < n; i++) scatter(A, R, x0 + R() * (x1 - x0), y, z0 + R() * (z1 - z0), pick(R, mix));
}

// ============================================================================ ruined walls
// A wall broken into equal bays along o[0]..o[1], each standing to its own height; the plaster skin keeps to the inner
// span i[0]..i[1] (the end walls stop at the inner faces of the long walls, so no two skins overlap at a corner).
// floors: [[baseY, walkY], ...] per storey, bottom up; each bay tall enough on a storey is a damage panel.
// Broken tops get a few teeth (narrower courses, no plaster) and loose stones set into the break.
function rwall(A, R, { axis, outer, inner, o, i, y0, heights, holes = [], side, floors, stone = "stone", plaster = "plaster", teeth = true }) {
  const n = heights.length, w = (o[1] - o[0]) / n, out = Math.sign(outer - inner), mid = inner + out * PT;
  const d0 = Math.min(outer, inner), d1 = Math.max(outer, inner);
  for (let k = 0; k < n; k++) {
    const b0 = o[0] + k * w, b1 = b0 + w, h = heights[k];
    if (h <= y0 + 0.05) continue;
    const ib0 = Math.max(b0, i[0]), ib1 = Math.min(b1, i[1]);
    const hs = holes.filter((q) => q.a1 > b0 && q.a0 < b1).map((q) => ({ ...q, a0: Math.max(q.a0, b0), a1: Math.min(q.a1, b1) }));
    const bands = [];
    floors.forEach(([base, walk], s) => {
      const top = Math.min(h, s + 1 < floors.length ? floors[s + 1][0] : h), b = s === 0 ? y0 : base;
      if (top - b >= 1.2) bands.push([b, top, s, walk]);
    });
    wall(A, { axis, outer, inner, aOut: [b0, b1], aIn: [ib0, ib1], y0, y1: h, holes: hs, pt: PT, stone, plaster, panel: bands.length && ib1 - ib0 > 1.1 ? { side: side + k, family: "masonry", bands } : null });
    if (!teeth || h < y0 + 0.6) continue;
    const nt = 1 + Math.floor(R() * 2.2);
    for (let t = 0; t < nt; t++) {
      const tw = Math.min(0.3 + R() * 0.55, w - 0.25), ta = b0 + 0.08 + R() * Math.max(0.01, w - tw - 0.16), th = 0.15 + R() * 0.5;
      if (tw < 0.2 || hs.some((q) => q.a1 > ta - 0.15 && q.a0 < ta + tw + 0.15 && q.y1 > h - 0.4)) continue;
      wbox(A, axis, ta, ta + tw, h, h + th, mid, outer, stone);
      const p0 = Math.max(ta, ib0), p1 = Math.min(ta + tw, ib1);
      if (p1 - p0 > 0.05 && R() < 0.5) wbox(A, axis, p0, p1, h, h + th * 0.5, inner, mid, plaster);
    }
    for (let s = 0; s < 2; s++) {   // loose stones bedded into the broken top (their bases sink below it)
      const a = b0 + 0.15 + R() * (w - 0.3), sz = [0.18 + R() * 0.2, 0.12 + R() * 0.12, Math.min(0.4, d1 - d0 - 0.05)];
      const c = axis === "x" ? [a, h + sz[1] * 0.25, (d0 + d1) / 2 + (R() - 0.5) * 0.1] : [(d0 + d1) / 2 + (R() - 0.5) * 0.1, h + sz[1] * 0.25, a];
      chunk(A, c, axis === "x" ? sz : [sz[2], sz[1], sz[0]], [(R() - 0.5) * 20, R() * 40 - 20, (R() - 0.5) * 20], R() < 0.3 ? "rubble_core" : stone);
    }
  }
}
// a glassless window opening: stone sill (nose embedded in the face), optional stone and oak lintels; returns the hole
function opening(A, { axis, outer, inner, c, sill, w, h, lintel = true }) {
  const out = Math.sign(outer - inner), a0 = c - w / 2, a1 = c + w / 2, top = sill + h;
  wbox(A, axis, a0, a1, sill - 0.08, sill, inner, outer, "stone_dressed");
  wbox(A, axis, a0 - 0.08, a1 + 0.08, sill - 0.085, sill - 0.005, outer - out * 0.02, outer + out * 0.06, "stone_dressed");
  if (lintel) {
    wbox(A, axis, a0 - 0.16, a1 + 0.16, top - 0.005, top + 0.24, outer - out * 0.12, outer + out * 0.02, "stone_dressed");
    wbox(A, axis, a0 - 0.12, a1 + 0.12, top - 0.004, top + 0.16, inner - out * 0.012, inner + out * 0.2, "oak_x");
  }
  return { a0, a1, y0: sill - 0.08, y1: top };
}
const gap = (c, w, top) => ({ a0: c - w / 2, a1: c + w / 2, y0: -10, y1: top });
// plinth, floor slab and four ruined walls; X / Z outer half-extents, T wall thickness
function ruinShell(A, R, { X, Z, T, GF, floors, front, back, east, west, floorMat = "terracotta", plaster = "plaster" }) {
  const XI = X - T, ZI = Z - T, y0 = GF - 0.02;
  A.box([-X - 0.05, -0.9, -Z - 0.05], [X + 0.05, y0, Z + 0.05], "stone_plinth");
  if (floorMat) A.box([-XI, y0, -ZI], [XI, GF, ZI], floorMat);
  rwall(A, R, { axis: "x", outer: Z, inner: ZI, o: [-X, X], i: [-XI, XI], y0, floors, side: "front", plaster, ...front });
  rwall(A, R, { axis: "x", outer: -Z, inner: -ZI, o: [-X, X], i: [-XI, XI], y0, floors, side: "rear", plaster, ...back });
  rwall(A, R, { axis: "z", outer: X, inner: XI, o: [-ZI - PT, ZI + PT], i: [-ZI, ZI], y0, floors, side: "east", plaster, ...east });
  rwall(A, R, { axis: "z", outer: -X, inner: -XI, o: [-ZI - PT, ZI + PT], i: [-ZI, ZI], y0, floors, side: "west", plaster, ...west });
  return { XI, ZI, y0 };
}
// one slope of a slate roof over x0..x1 (ridge along X at z 0; s = +1 front slope, -1 rear), with fascia and gutter
function roofSlope(A, { x0, x1, Z, EAVE, RIDGE, s, ov = 0.35, t = 0.14, m = "slate" }) {
  const th = Math.atan2(RIDGE - EAVE, Z), deg = th / D2R;
  const zA = -0.12 * s, zB = s * (Z + ov), zm = (zA + zB) / 2, ym = (RIDGE + 0.12 * Math.tan(th) + (RIDGE - (Z + ov) * Math.tan(th))) / 2;
  const len = Math.abs(zB - zA) / Math.cos(th), a = s > 0 ? deg : -deg, nrm = [0, Math.cos(a * D2R), Math.sin(a * D2R)];
  A.obox([(x0 + x1) / 2, ym + (nrm[1] * t) / 2, zm + (nrm[2] * t) / 2], [x1 - x0, t, len], [a, 0, 0], m);
  const ye = EAVE - ov * Math.tan(th);
  A.box([x0, ye - 0.24, Math.min(s * (Z + ov - 0.03), s * (Z + ov))], [x1, ye + 0.02, Math.max(s * (Z + ov - 0.03), s * (Z + ov))], "oak_x");
  A.box([x0, ye - 0.2, Math.min(s * (Z + ov), s * (Z + ov + 0.13))], [x1, ye - 0.1, Math.max(s * (Z + ov), s * (Z + ov + 0.13))], "zinc", { k: 0 });
}
// bare (charred) rafters where a roof slope has gone: frac of each rafter's length survives from the eave up
function rafters(A, R, { xs, Z, EAVE, RIDGE, s, ov = 0.3, m = "charred", minF = 0.35 }) {
  const th = Math.atan2(RIDGE - EAVE, Z), a = (s > 0 ? 1 : -1) * th / D2R;
  const pe = [s * (Z + ov), EAVE - ov * Math.tan(th)], pr = [-0.1 * s, RIDGE + 0.1 * Math.tan(th)], len = Math.hypot(pr[0] - pe[0], pr[1] - pe[1]);
  const nrm = [Math.sin(a * D2R), Math.cos(a * D2R)];
  for (const x of xs) {
    const f = minF + R() * (1 - minF), cz = pe[0] + (pr[0] - pe[0]) * f / 2 - nrm[0] * 0.09, cy = pe[1] + (pr[1] - pe[1]) * f / 2 - nrm[1] * 0.09;
    A.obox([x, cy, cz], [0.1, 0.15, len * f], [a, (R() - 0.5) * 4, 0], m, { k: 0 });
  }
}
// upper floor joists (along Z) under a floor slab at FF, k:0
function joists(A, x0, x1, z0, z1, FF, every = 0.6, m = "oak_z") {
  for (let x = x0 + 0.25; x < x1 - 0.1; x += every) A.box([x - 0.07, FF - 0.42, z0], [x + 0.07, FF - 0.2, z1], m, { k: 0 });
}
// a broken floor edge at x = xe: snapped joists drooping toward +X (dir), splintered boards
function brokenEdge(A, R, { xe, z0, z1, FF, dir = 1, every = 0.6 }) {
  for (let z = z0 + 0.3; z < z1 - 0.1; z += every) {
    const L = 0.6 + R() * 1.4, dip = 12 + R() * 40;
    A.obox([xe + dir * (L / 2) * Math.cos(dip * D2R), FF - 0.31 - (L / 2) * Math.sin(dip * D2R), z], [L, 0.22, 0.14], [0, 0, -dir * dip], R() < 0.4 ? "charred" : "oak_x", { k: 0 });
  }
  for (let z = z0 + 0.15; z < z1 - 0.2; z += 0.45 + R() * 0.4) {
    const L = 0.25 + R() * 0.7;
    A.obox([xe + dir * L / 2, FF - 0.06, z], [L, 0.04, 0.16 + R() * 0.1], [0, (R() - 0.5) * 16, -dir * (R() * 18)], "floor_x", { k: 0 });
  }
}
const unglazed = (p, yaw, kind = "window") => ({ ...fire(p, yaw, kind), unglazed: true });

// ============================================================================ R1 shelled two-storey house
// 10 x 7.5 m Norman house, two storeys, roof fallen in. The west half stands to the eaves with its gable, a strip of
// rear roof and a stair to the surviving half of the upper floor; the east half was hit: the floor and roof lie on
// the ground floor as a rubble heap, the east wall is down to 1.4-3 m and a shell breach opens the front.
export function ruinHouseShelled() {
  const A = new Asset({ id: "ruin_house_shelled", version: 1, name: "Shelled house (two storeys, roof collapsed)", category: "Ruins",
    description: "Two-storey Norman house, 10 x 7.5 m, hit by shellfire: the west half stands to the eaves with its gable, a strip of rear roof and the stair to the surviving half of the upper floor (open broken edge over the rubble); the east half's floor and roof lie in a heap on the ground floor, the east wall is down to 1.4-3 m and a shell breach opens the front. Front door, rear doorway, glassless windows.",
    floors: 2, enterable: true, footprint: [10.2, 7.7], plinthDepth: 1.0, maxGrade: 1.0 });
  ruinMaterials(A);
  const R = rng(3101);
  const X = 5.0, Z = 3.75, T = 0.55, GF = 0.25, FF = 3.25, EAVE = 6.1, RIDGE = 8.6;
  const fr = { axis: "x", outer: Z, inner: Z - T }, bk = { axis: "x", outer: -Z, inner: -Z + T };
  const wf = { axis: "z", outer: -X, inner: -X + T };
  const front = [door(A, { id: "front_door", ...fr, c: -2.2, floor: GF, w: 1.0, h: 2.1, hingeSide: 1 }),
    opening(A, { ...fr, c: 0.6, sill: GF + 0.9, w: 1.0, h: 1.3 }), opening(A, { ...fr, c: -2.2, sill: FF + 0.9, w: 0.9, h: 1.2 }),
    opening(A, { ...fr, c: 0.6, sill: FF + 0.9, w: 0.9, h: 1.2, lintel: false }), { a0: 2.4, a1: 4.0, y0: -10, y1: GF + 2.4 }];
  const back = [gap(2.6, 1.0, GF + 2.1), opening(A, { ...bk, c: -2.2, sill: GF + 0.9, w: 0.9, h: 1.2 }), opening(A, { ...bk, c: -2.2, sill: FF + 0.9, w: 0.9, h: 1.2 })];
  const floors = [[GF - 0.02, GF], [FF, FF]];
  const { XI, ZI } = ruinShell(A, R, { X, Z, T, GF, floors,
    front: { heights: [6.1, 6.1, 5.0, 3.6, 2.2], holes: front },
    back: { heights: [6.1, 6.1, 4.2, 2.8, 3.2], holes: back },
    east: { heights: [2.6, 1.4, 3.0] },
    west: { heights: [6.1, 6.1, 6.1], holes: [opening(A, { ...wf, c: 0.0, sill: GF + 0.9, w: 0.7, h: 1.0 })] } });
  // west gable, a strip of the rear roof slope on it, charred rafters and the ridge beam
  A.prism(0, -X, -XI - 0.03, [[-Z, EAVE], [Z, EAVE], [0, RIDGE]], "stone");
  roofSlope(A, { x0: -X - 0.28, x1: -1.2, Z, EAVE, RIDGE, s: -1 });
  rafters(A, R, { xs: [-4.0, -3.0, -2.0], Z, EAVE, RIDGE, s: 1, minF: 0.5 });
  A.obox([-2.6, RIDGE - 0.15, 0], [4.6, 0.2, 0.2], [0, 0, -4], "charred", { k: 0 });
  // ground floor: partition with a doorway at x 0, broken above
  for (const [a0, a1, ya, yb] of [[-ZI, -0.5, GF, FF - 0.2], [0.5, ZI, GF, 2.4], [-0.5, 0.5, GF + 2.1, 2.6]]) wbox(A, "z", a0, a1, ya, yb, -0.08, 0.08, "plaster");
  // stair against the west wall to the upper floor; the upper floor survives over x < 0.9
  stairs(A, { x0: -XI, x1: -XI + 0.95, z0: -2.3, dir: 1, y0: GF, rise: FF - GF, n: 15, run: 3.5, openSide: 1 });
  A.box([-XI + 1.0, FF - 0.2, -ZI], [0.9, FF, ZI], "floor_x");
  A.box([-XI, FF - 0.2, 1.2], [-XI + 1.0, FF, ZI], "floor_x");
  joists(A, -XI + 1.0, 0.9, -ZI, ZI, FF);
  brokenEdge(A, R, { xe: 0.9, z0: -ZI, z1: ZI, FF });
  A.obox([2.25, 1.75, -1.1], [2.6, 0.14, 2.4], [0, 8, -36], "floor_x", { k: 0 });   // a fallen floor section leaning into the heap
  // the heap of floor, roof and east wall in the east room, and its spill out through the east wall
  heap(A, R, { cx: 3.2, cz: 0.2, rx: 1.5, rz: 2.2, h: 1.0, y: GF - 0.02, mix: MIX.house, clip: [0.1, XI + 0.02, -ZI - 0.02, ZI + 0.02] });
  heap(A, R, { cx: X + 0.9, cz: -0.3, rx: 1.1, rz: 2.0, h: 0.75, mix: MIX.house, clip: [X - 0.1, X + 2.2, -2.5, 2.0] });
  for (let k = 0; k < 7; k++) chunk(A, [2.2 + R() * 2.0, GF + 0.5 + R() * 0.5, -1.6 + R() * 3.4], [0.3, 0.015, 0.2], [R() * 50 - 25, R() * 360, R() * 50 - 25], "slate");
  strew(A, R, { x0: -XI + 1.2, x1: -0.3, z0: -ZI + 0.3, z1: ZI - 0.3, y: GF, n: 10, mix: MIX.house });
  strew(A, R, { x0: -X, x1: X, z0: Z + 0.4, z1: Z + 2.2, y: 0, n: 14, mix: MIX.house });
  setNav(A, {
    effect: "block",
    rooms: [room("front_room", [-XI, GF, -ZI], [-0.08, FF - 0.2, ZI]), room("east_room", [0.08, GF, -ZI], [XI, FF + 2.8, ZI]), room("upper", [-XI, FF, -ZI], [0.9, FF + 2.8, ZI])],
    doors: [{ a: "front_room", b: "outside", p: [-2.2, GF, Z - T / 2] }, { a: "east_room", b: "outside", p: [3.2, GF, Z - T / 2] }, { a: "east_room", b: "outside", p: [2.6, GF, -Z + T / 2] },
            { a: "front_room", b: "east_room", p: [0, GF, 0] }, { a: "front_room", b: "upper", p: [-XI + 0.48, FF, 1.0] }],
    entrances: [{ p: [-2.2, 0, Z + 1.1], dir: [0, 0, -1], room: "front_room" }, { p: [3.2, 0, Z + 1.1], dir: [0, 0, -1], room: "east_room" }, { p: [2.6, 0, -Z - 1.1], dir: [0, 0, 1], room: "east_room" }],
    corners: [...corners("front_room", -XI + 1.0, -0.08, -ZI, ZI, GF), ...corners("east_room", 0.08, XI, -ZI, ZI, GF, 0.5), ...corners("upper", -XI + 1.0, 0.9, -ZI, ZI, FF)],
    firingPoints: [unglazed([-2.2, FF, ZI - 0.4], 0), unglazed([0.6, GF, ZI - 0.4], 0), unglazed([-2.2, FF, -ZI + 0.4], 180), fire([-2.2, GF, -ZI + 0.5], 180, "window")],
    cover: [cover([-X - 0.6, 0, -2.0], 90, "high", ["left"]), cover([-X - 0.6, 0, 2.0], 90, "high", ["right"]), cover([-4.0, 0, Z + 0.6], 180, "high", ["left", "right"]), cover([-4.0, 0, -Z - 0.6], 0, "high", ["left", "right"])],
  });
  setTour(A, { pos: [-2.2, 0, 7.5], yaw: 0 }, [
    { goto: [-2.2, 4.9], note: "approach the front door" },
    { interact: "front_door", aim: [-2.05, 1.3, ZI + 0.05], expectOpen: true },
    { goto: [-2.2, 2.2], expectY: GF, note: "into the front room" },
    { goto: [-2.9, -2.75] }, { goto: [-3.97, -2.75], expectY: GF, note: "stair foot" },
    { goto: [-3.97, 1.0], expectY: FF, note: "up the stair" }, { goto: [-3.97, 2.0], expectY: FF, note: "landing" },
    { goto: [-1.5, 2.0], expectY: FF, note: "upper floor" }, { goto: [0.35, 0.0], expectY: FF, note: "at the broken floor edge" },
    { look: [3.5, 0.6, 0], hold: 0.6 }, { shot: "ruin_house_shelled_upper" },
    { goto: [-1.5, 2.0] }, { goto: [-3.97, 2.0] }, { goto: [-3.97, 1.0] }, { goto: [-3.97, -2.75], expectY: GF, note: "back down" }, { goto: [-2.9, -2.75] },
    { goto: [-0.7, 0.0] }, { goto: [0.8, 0.0], expectY: GF, note: "through to the shelled east room" },
    { look: [3.2, 1.0, 0.2], hold: 0.5 }, { shot: "ruin_house_shelled_heap" },
    { goto: [1.0, -2.65] }, { goto: [2.6, -2.65], expectY: GF }, { goto: [2.6, -5.0], note: "out by the rear doorway" },
  ]);
  return A;
}

// ============================================================================ R2 gutted farmhouse shell
// 14 x 7 m single-storey farmhouse burnt out: the stone shell stands to the eaves with both gables (the east one
// broken), roof, loft and floors gone; charred beams across the walls and on the floor, ash heaps, the chimney stack.
export function ruinFarmhouseGutted() {
  const A = new Asset({ id: "ruin_farmhouse_gutted", version: 1, name: "Gutted farmhouse shell", category: "Ruins",
    description: "Burnt-out Norman farmhouse, 14 x 7 m: the rubble-stone shell stands to the eaves with the west gable and chimney stack and a broken east gable; roof, loft and floors are gone. Charred tie beams still span the walls, more lie on the earth floor among ash and rubble heaps. A cross wall with a doorway divides the kitchen from the hall; two front doorways and a rear doorway, empty windows.",
    floors: 1, enterable: true, footprint: [14.2, 7.2], plinthDepth: 1.0, maxGrade: 1.0 });
  ruinMaterials(A);
  const R = rng(3202);
  const X = 7.0, Z = 3.5, T = 0.6, GF = 0.25, EAVE = 3.6, RIDGE = 7.2;
  const fr = { axis: "x", outer: Z, inner: Z - T }, bk = { axis: "x", outer: -Z, inner: -Z + T };
  const win = (o, c) => opening(A, { ...o, c, sill: GF + 0.9, w: 1.0, h: 1.2 });
  const floors = [[GF - 0.02, GF]];
  const { XI, ZI } = ruinShell(A, R, { X, Z, T, GF, floors, floorMat: "earth", plaster: "plaster_smoke",
    front: { heights: [3.6, 3.6, 3.6, 3.0, 3.6, 3.6, 3.6], holes: [gap(-0.5, 1.1, GF + 2.1), gap(4.2, 1.0, GF + 2.0), win(fr, -4.5), win(fr, 1.8), win(fr, 6.0)] },
    back: { heights: [3.6, 3.2, 2.4, 3.6, 3.6, 2.0, 3.6], holes: [gap(-4.0, 1.0, GF + 2.0), win(bk, 0.5), win(bk, 3.5)] },
    east: { heights: [3.6, 3.6, 3.6] }, west: { heights: [3.6, 3.6, 3.6] } });
  // gables: west whole, east broken off; chimney stack on the west gable, open hearth below it
  A.prism(0, -X, -XI - 0.03, [[-Z, EAVE], [Z, EAVE], [0, RIDGE]], "stone");
  A.prism(0, XI + 0.03, X, [[-Z, EAVE], [1.2, EAVE], [-0.6, RIDGE - 1.2]], "stone");
  A.box([-X - 0.05, RIDGE - 1.0, -0.6], [-X + 0.9, RIDGE + 1.0, 0.6], "stone_dressed");
  A.box([-XI - 0.015, GF, -1.0], [-XI + 0.5, GF + 1.05, -0.7], "stone_dressed"); A.box([-XI - 0.015, GF, 0.7], [-XI + 0.5, GF + 1.05, 1.0], "stone_dressed");
  A.box([-XI - 0.015, GF + 1.05, -1.15], [-XI + 0.6, GF + 1.3, 1.15], "charred");
  A.box([-XI - 0.02, GF + 1.3, -0.9], [-XI + 0.04, EAVE - 0.3, 0.9], "soot");   // soot down the flue face (embedded)
  A.box([-XI - 0.02, GF, -0.69], [-XI + 0.04, GF + 1.05, 0.69], "soot");
  // cross wall (stone) at x -2.4 with a doorway near the rear, broken top
  rwall(A, R, { axis: "z", outer: -2.15, inner: -2.65, o: [-ZI, ZI], i: [-ZI, ZI], y0: GF, heights: [3.0, 2.2, 3.3], holes: [gap(-1.7, 1.0, GF + 2.0)], side: "cross", floors, plaster: "plaster_smoke" });
  // charred tie beams across the walls (some still in their pockets), others fallen
  for (const x of [-5.0, 0.6, 5.4]) A.box([x - 0.13, EAVE - 0.32, -ZI - 0.2], [x + 0.13, EAVE - 0.04, ZI + 0.2], "charred", { k: 0 });
  for (const [x, z, yaw, tilt] of [[-4.6, 0.3, 8, 12], [2.3, -0.2, -10, 20], [4.4, 0.5, 5, 6], [0.9, 0.2, 172, 4]]) A.obox([x, GF + 2.7 * Math.sin(tilt * D2R) + 0.1, z], [0.24, 0.26, 5.4], [tilt, yaw, 0], "charred", { k: 0 });
  heap(A, R, { cx: 3.0, cz: -2.0, rx: 1.6, rz: 0.9, h: 0.6, y: GF - 0.02, mix: MIX.burnt, clip: [-XI - 0.02, XI + 0.02, -ZI - 0.02, ZI + 0.02] });
  heap(A, R, { cx: -4.6, cz: 2.1, rx: 1.3, rz: 0.75, h: 0.5, y: GF - 0.02, mix: MIX.burnt, clip: [-XI - 0.02, XI + 0.02, -ZI - 0.02, ZI + 0.02] });
  heap(A, R, { cx: 6.0, cz: 0.4, rx: 0.8, rz: 1.6, h: 0.75, y: GF - 0.02, mix: MIX.house, clip: [-XI - 0.02, XI + 0.02, -ZI - 0.02, ZI + 0.02] });
  strew(A, R, { x0: -XI + 0.3, x1: XI - 0.3, z0: -ZI + 0.3, z1: ZI - 0.3, y: GF, n: 30, mix: MIX.burnt });
  strew(A, R, { x0: -X, x1: X, z0: -Z - 1.6, z1: -Z - 0.3, y: 0, n: 10, mix: MIX.house });
  setNav(A, {
    effect: "block",
    rooms: [room("kitchen", [-XI, GF, -ZI], [-2.65, EAVE, ZI]), room("hall", [-2.15, GF, -ZI], [XI, EAVE, ZI])],
    doors: [{ a: "hall", b: "outside", p: [-0.5, GF, Z - T / 2] }, { a: "hall", b: "outside", p: [4.2, GF, Z - T / 2] }, { a: "kitchen", b: "outside", p: [-4.0, GF, -Z + T / 2] },
            { a: "kitchen", b: "hall", p: [-2.4, GF, -1.7] }],
    entrances: [{ p: [-0.5, 0, Z + 1.1], dir: [0, 0, -1], room: "hall" }, { p: [4.2, 0, Z + 1.1], dir: [0, 0, -1], room: "hall" }, { p: [-4.0, 0, -Z - 1.1], dir: [0, 0, 1], room: "kitchen" }],
    corners: [...corners("kitchen", -XI, -2.65, -ZI, ZI, GF), ...corners("hall", -2.15, XI, -ZI, ZI, GF, 0.6)],
    firingPoints: [unglazed([-4.5, GF, ZI - 0.4], 0), unglazed([1.8, GF, ZI - 0.4], 0), unglazed([0.5, GF, -ZI + 0.4], 180)],
    cover: [cover([X + 0.6, 0, 2.0], 270, "high", ["right"]), cover([-X - 0.6, 0, 2.0], 90, "high", ["left"]), cover([X + 0.6, 0, -2.0], 270, "high", ["left"]), cover([-X - 0.6, 0, -2.0], 90, "high", ["right"])],
  });
  setTour(A, { pos: [-0.5, 0, 6.5], yaw: 0 }, [
    { goto: [-0.5, 4.6], note: "approach the front doorway" }, { goto: [-0.5, 2.0], expectY: GF, note: "into the burnt-out hall" },
    { look: [5, 3.5, 0], hold: 0.5 }, { shot: "ruin_farmhouse_gutted_hall" },
    { goto: [-1.4, -1.7] }, { goto: [-3.4, -1.7], expectY: GF, note: "through the cross wall into the kitchen" },
    { goto: [-4.0, -0.6] }, { look: [-XI, 1.0, 0], hold: 0.5 }, { shot: "ruin_farmhouse_gutted_hearth" },
    { goto: [-4.0, -2.2] }, { goto: [-4.0, -5.0], note: "out by the rear doorway" },
  ]);
  return A;
}

// ============================================================================ R3 half-collapsed shop / terrace row
// Two-unit terrace, 12 x 8 m: the west unit (a shop with a wide shopfront, its house door and a stair to the upper
// floor) still stands under a damaged roof; the east unit has collapsed into its own ground floor. The party wall stands
// to the gable with the east unit's wallpaper, joist pockets and an upstairs fireplace hanging in the air; a breach
// through it at ground level joins the two.
export function ruinShopRow() {
  const A = new Asset({ id: "ruin_shop_row", version: 1, name: "Half-collapsed shop terrace", category: "Ruins",
    description: "Two-unit shop terrace, 12 x 8 m. The west unit stands: a shop with its shopfront blown open, a house door, a stair to the upper floor (front windows over the street) and a holed roof. The east unit has fallen into its ground floor as a climbable heap; the party wall stands to the gable showing its wallpaper, joist pockets and an upstairs fireplace, with a breach through to the shop.",
    floors: 2, enterable: true, footprint: [12.2, 8.2], plinthDepth: 1.0, maxGrade: 1.0 });
  ruinMaterials(A);
  const R = rng(3303);
  const X = 6.0, Z = 4.0, T = 0.55, GF = 0.25, FF = 3.35, EAVE = 6.2, RIDGE = 8.8, PW = 0.25;
  const fr = { axis: "x", outer: Z, inner: Z - T }, bk = { axis: "x", outer: -Z, inner: -Z + T };
  const floors = [[GF - 0.02, GF], [FF, FF]];
  const shopfront = { a0: -4.9, a1: -2.0, y0: -10, y1: GF + 2.6 };
  const front = [shopfront, door(A, { id: "house_door", ...fr, c: -1.05, floor: GF, w: 0.9, h: 2.1, hingeSide: -1 }),
    opening(A, { ...fr, c: -4.0, sill: FF + 0.9, w: 1.0, h: 1.35 }), opening(A, { ...fr, c: -1.6, sill: FF + 0.9, w: 1.0, h: 1.35 }), gap(3.0, 2.6, GF + 2.6)];
  const back = [gap(-3.2, 0.95, GF + 2.0), opening(A, { ...bk, c: -1.4, sill: FF + 0.9, w: 0.9, h: 1.2 }), opening(A, { ...bk, c: 3.4, sill: GF + 0.9, w: 0.9, h: 1.2, lintel: false })];
  const { XI, ZI } = ruinShell(A, R, { X, Z, T, GF, floors,
    front: { heights: [6.2, 6.2, 6.2, 2.4, 1.2, 1.8], holes: front },
    back: { heights: [6.2, 6.2, 6.2, 3.5, 2.2, 4.0], holes: back },
    east: { heights: [3.0, 4.6, 2.2] }, west: { heights: [6.2, 6.2, 6.2] } });
  // shopfront: fascia board and sign over the opening, stall riser stubs either side
  A.box([shopfront.a0 - 0.1, GF + 2.63, Z - 0.03], [shopfront.a1 + 0.1, GF + 3.0, Z + 0.1], "shop_paint");
  A.box([shopfront.a0 - 0.1, GF + 2.55, ZI - 0.02], [shopfront.a1 + 0.1, GF + 2.72, Z - 0.04], "oak_x");
  // party wall x -PW..PW: full height plus its gable; plaster on the shop side, wallpaper and soot on the ruin side
  rwall(A, R, { axis: "z", outer: PW, inner: -PW, o: [-ZI, ZI], i: [-ZI, ZI], y0: GF, heights: [EAVE, EAVE, EAVE], holes: [gap(2.1, 1.1, GF + 2.2)], side: "party", floors, teeth: false });
  A.prism(0, -PW, PW, [[-ZI, EAVE], [ZI, EAVE], [0, RIDGE - 0.3]], "stone");
  A.prism(0, -X, -XI - 0.03, [[-Z, EAVE], [Z, EAVE], [0, RIDGE]], "stone");
  for (const [z0, z1, y0, y1] of [[-3.2, -0.6, FF + 0.15, EAVE - 0.4], [0.8, 3.1, FF + 0.5, EAVE - 0.9], [-2.2, 1.3, GF + 0.6, FF - 0.5]]) A.box([PW - 0.01, y0, z0], [PW + 0.012, y1, z1], "wallpaper");
  A.box([PW - 0.015, FF + 0.1, -0.7], [PW + 0.45, FF + 1.15, -0.4], "stone_dressed"); A.box([PW - 0.015, FF + 0.1, 0.4], [PW + 0.45, FF + 1.15, 0.7], "stone_dressed");   // upstairs fireplace, its floor gone
  A.box([PW - 0.015, FF + 1.15, -0.85], [PW + 0.55, FF + 1.3, 0.85], "stone_dressed");
  A.box([PW - 0.02, FF + 0.1, -0.39], [PW + 0.015, FF + 1.15, 0.39], "soot");
  for (let z = -ZI + 0.4; z < ZI - 0.2; z += 0.6) A.box([PW - 0.05, FF - 0.4, z - 0.07], [PW + 0.12 + R() * 0.35, FF - 0.2, z + 0.07], R() < 0.5 ? "charred" : "oak_x", { k: 0 });   // joist stubs in their pockets
  // west unit: stair along the party wall, upper floor with the stair hole, roof (front slope holed)
  stairs(A, { x0: -PW - 1.0, x1: -PW - 0.02, z0: -2.6, dir: 1, y0: GF, rise: FF - GF, n: 15, run: 3.6, openSide: -1 });
  A.box([-XI, FF - 0.2, -ZI], [-PW - 1.0, FF, ZI], "floor_x");
  A.box([-PW - 1.0, FF - 0.2, 1.0], [-PW, FF, ZI], "floor_x");
  joists(A, -XI, -PW - 1.0, -ZI, ZI, FF);
  railing(A, "z", -ZI, -2.6, -PW - 1.03, FF);
  roofSlope(A, { x0: -X - 0.28, x1: PW, Z, EAVE, RIDGE, s: -1 });
  roofSlope(A, { x0: -X - 0.28, x1: -3.4, Z, EAVE, RIDGE, s: 1 });
  rafters(A, R, { xs: [-2.8, -1.9, -1.0], Z, EAVE, RIDGE, s: 1, minF: 0.4 });
  A.obox([-2.9, RIDGE - 0.12, 0], [6.4, 0.2, 0.2], [0, 0, 0], "oak_x", { k: 0 });
  // east unit: its floor, roof and front lie in a climbable heap; slates and joists strewn about
  heap(A, R, { cx: 3.3, cz: 0.0, rx: 2.3, rz: 2.7, h: 1.6, y: GF - 0.02, mix: MIX.house, clip: [PW - 0.02, XI + 0.02, -ZI - 0.02, ZI + 0.02], density: 8 });
  heap(A, R, { cx: 3.0, cz: Z + 1.0, rx: 2.4, rz: 1.0, h: 0.5, mix: MIX.house });
  for (let k = 0; k < 5; k++) A.obox([1.6 + R() * 3.4, GF + 0.9 + R() * 0.5, -2.0 + R() * 4.0], [0.16, 0.22, 2.2 + R() * 1.6], [(R() - 0.5) * 50, R() * 180, (R() - 0.5) * 30], R() < 0.5 ? "oak_z" : "charred", { k: 0 });
  // shop: debris on the floor; glass and plaster
  strew(A, R, { x0: -XI + 0.3, x1: -PW - 1.2, z0: -ZI + 0.3, z1: ZI - 0.3, y: GF, n: 12, mix: MIX.stone });
  setNav(A, {
    effect: "block",
    rooms: [room("shop", [-XI, GF, -ZI], [-PW, FF - 0.2, ZI]), room("upper", [-XI, FF, -ZI], [-PW, EAVE, ZI]), room("ruin", [PW, GF, -ZI], [XI, EAVE, ZI])],
    doors: [{ a: "shop", b: "outside", p: [-3.45, GF, Z - T / 2] }, { a: "shop", b: "outside", p: [-1.05, GF, Z - T / 2] }, { a: "shop", b: "outside", p: [-3.2, GF, -Z + T / 2] },
            { a: "ruin", b: "outside", p: [3.0, GF, Z - T / 2] }, { a: "shop", b: "ruin", p: [0, GF, 2.1] }, { a: "shop", b: "upper", p: [-PW - 0.5, FF, 1.3] }],
    entrances: [{ p: [-3.45, 0, Z + 1.2], dir: [0, 0, -1], room: "shop" }, { p: [-1.05, 0, Z + 1.1], dir: [0, 0, -1], room: "shop" }, { p: [-3.2, 0, -Z - 1.1], dir: [0, 0, 1], room: "shop" },
                { p: [3.0, 0, Z + 2.4], dir: [0, 0, -1], room: "ruin" }],
    corners: [...corners("shop", -XI, -PW - 1.0, -ZI, ZI, GF), ...corners("upper", -XI, -PW - 1.0, -ZI, ZI, FF)],
    firingPoints: [unglazed([-4.0, FF, ZI - 0.4], 0), unglazed([-1.6, FF, ZI - 0.4], 0), unglazed([-1.4, FF, -ZI + 0.4], 180)],
    cover: [cover([-X - 0.6, 0, 2.0], 90, "high", ["left"]), cover([-X - 0.6, 0, -2.0], 90, "high", ["right"]), cover([-1.6, 0, -Z - 0.6], 0, "high", ["left", "right"])],
  });
  setTour(A, { pos: [-3.45, 0, 8.0], yaw: 0 }, [
    { goto: [-3.45, 5.4], note: "approach the blown-in shopfront" }, { goto: [-3.45, 2.0], expectY: GF, note: "into the shop" },
    { goto: [-1.8, -3.05] }, { goto: [-0.77, -3.05], expectY: GF, note: "stair foot" },
    { goto: [-0.77, 0.8], expectY: FF, note: "up the stair" }, { goto: [-0.77, 1.8], expectY: FF },
    { goto: [-2.8, 2.6], expectY: FF, note: "upper front room" }, { look: [-2.8, FF + 1.2, 12], hold: 0.5 }, { shot: "ruin_shop_row_upper" },
    { goto: [-0.77, 1.8] }, { goto: [-0.77, 0.8] }, { goto: [-0.77, -3.05], expectY: GF, note: "back down" },
    { goto: [-1.8, -3.05] }, { goto: [-1.8, 2.1] }, { goto: [-0.7, 2.1] }, { goto: [0.9, 2.1], expectY: GF, note: "through the party-wall breach" },
    { goto: [3.3, 0.0], expectY: GF + 1.58, note: "up onto the rubble of the east unit" },
    { look: [-0.2, FF + 1.0, 0], hold: 0.5 }, { shot: "ruin_shop_row_party_wall" },
    { goto: [3.0, 3.0] }, { goto: [3.0, 6.4], note: "down to the street" },
  ]);
  return A;
}

// ============================================================================ R4 burnt-out barn
// 16 x 9 m stone barn after the fire: walls standing 3-5 m, roof gone, the gables up (the east one with its pitching
// door), charred tie beams, fallen rafters, ash heaps, the burnt cart; the east end of the hay loft survived on its
// stone corbels and posts, reached by the scorched loft stair.
export function ruinBarnBurnt() {
  const A = new Asset({ id: "ruin_barn_burnt", version: 1, name: "Burnt-out barn", category: "Ruins",
    description: "Stone barn gutted by fire, 16 x 9 m: walls standing 3-5 m, roof gone, both gables up (the east one with its pitching door). Charred tie beams and fallen rafters, ash heaps and the burnt skeleton of a cart; the east end of the hay loft survived on its posts and is reached by the scorched loft stair. Cart doorway, side doorway, loophole slits.",
    floors: 1, enterable: true, footprint: [16.2, 9.2], plinthDepth: 1.0, maxGrade: 0.8 });
  ruinMaterials(A);
  const R = rng(3404);
  const X = 8.0, Z = 4.5, T = 0.6, GF = 0.15, EAVE = 5.0, RIDGE = 9.0, LOFT = 3.3;
  const slit = (c, y0 = GF + 1.25) => ({ a0: c - 0.09, a1: c + 0.09, y0, y1: y0 + 1.0 });
  const floors = [[GF - 0.02, GF]];
  const { XI, ZI } = ruinShell(A, R, { X, Z, T, GF, floors, floorMat: "earth", plaster: "stone",
    front: { heights: [5.0, 5.0, 4.2, 4.6, 5.0, 3.4, 5.0, 5.0], holes: [gap(-2.0, 3.4, GF + 3.6), gap(5.0, 1.0, GF + 2.1), slit(1.8), slit(-6.2)] },
    back: { heights: [5.0, 4.0, 3.0, 4.4, 5.0, 5.0, 4.6, 5.0], holes: [slit(-4.5), slit(-0.5), slit(3.5)] },
    east: { heights: [5.0, 5.0, 5.0], holes: [{ a0: -0.6, a1: 0.6, y0: LOFT + 0.05, y1: EAVE }] },
    west: { heights: [5.0, 5.0, 5.0], holes: [slit(-1.5), slit(1.5)] } });
  A.prism(0, -X, -XI - 0.03, [[-Z, EAVE], [Z, EAVE], [0, RIDGE]], "stone");
  A.prism(0, XI + 0.03, X, [[-Z, EAVE], [0.9, EAVE], [-0.4, RIDGE - 0.9]], "stone");
  A.prism(0, XI + 0.03, X, [[1.6, EAVE], [Z, EAVE], [1.6, EAVE + 1.2]], "stone");
  // burnt cart-door leaves hanging in the doorway, the side-door frame
  A.obox([-3.45, GF + 1.5, ZI + 0.25], [0.5, 2.6, 0.07], [0, 72, 0], "charred", { k: 0 });
  A.obox([-0.6, GF + 0.9, ZI + 0.3], [0.9, 1.4, 0.07], [0, -64, 8], "charred", { k: 0 });
  // charred tie beams, fallen rafters
  for (const x of [-4.5, 0.0]) A.box([x - 0.13, EAVE - 0.3, -ZI - 0.2], [x + 0.13, EAVE - 0.02, ZI + 0.2], "charred", { k: 0 });
  for (const [x, z, yaw, tilt] of [[-5.5, 0.6, 20, 18], [-2.6, -1.4, -30, 8], [0.9, 0.9, 60, 25], [-6.4, -2.0, 90, 4], [2.4, -2.6, 75, 6]]) A.obox([x, GF + 1.1 * Math.sin(tilt * D2R) + 0.1, z], [0.14, 0.18, 5.4], [tilt, yaw, 0], "charred", { k: 0 });
  // surviving loft over x 4.8 .. XI on posts and corbels; scorched stair up to it
  A.box([4.8, LOFT - 0.2, -ZI], [XI, LOFT, ZI], "floor_x");
  for (let z = -ZI + 0.4; z < ZI; z += 1.1) A.box([4.8, LOFT - 0.42, z - 0.08], [XI, LOFT - 0.2, z + 0.08], "charred", { k: 0 });
  for (const z of [-2.2, 2.2]) A.box([4.75, GF, z - 0.12], [4.99, LOFT - 0.2, z + 0.12], "oak_y");
  brokenEdge(A, R, { xe: 4.8, z0: 1.0, z1: ZI, FF: LOFT, dir: -1, every: 1.1 });
  stairs(A, { x0: 3.8, x1: 4.75, z0: -2.6, dir: 1, y0: GF, rise: LOFT - GF, n: 14, run: 3.5, openSide: -1 });
  for (let k = 0; k < 4; k++) A.box([5.6 + (k % 2) * 1.0, LOFT, -ZI + 0.1 + Math.floor(k / 2) * 0.55], [6.5 + (k % 2) * 1.0, LOFT + 0.45, -ZI + 0.62 + Math.floor(k / 2) * 0.55], "ash", { k: 0 });   // burnt bales
  // ash and rubble heaps, the burnt cart
  heap(A, R, { cx: -4.6, cz: -2.6, rx: 1.7, rz: 1.1, h: 0.7, y: GF - 0.02, mix: MIX.burnt, clip: [-XI - 0.02, XI + 0.02, -ZI - 0.02, ZI + 0.02] });
  heap(A, R, { cx: 1.2, cz: 2.8, rx: 1.4, rz: 0.9, h: 0.55, y: GF - 0.02, mix: MIX.burnt, clip: [-XI - 0.02, XI + 0.02, -ZI - 0.02, ZI + 0.02] });
  heap(A, R, { cx: -XI + 0.6, cz: 1.6, rx: 0.9, rz: 1.4, h: 0.6, y: GF - 0.02, mix: MIX.stone, clip: [-XI - 0.02, XI + 0.02, -ZI - 0.02, ZI + 0.02] });
  A.box([-6.0, GF, 0.9], [-3.8, GF + 1.0, 2.5], "charred", { v: 0 });   // the cart: one collider, charred frame
  A.obox([-4.9, GF + 0.65, 1.7], [2.0, 0.07, 1.3], [0, 4, 9], "charred", { k: 0 });
  for (const z of [0.95, 2.45]) A.obox([-4.5, GF + 0.55, z], [1.1, 1.1, 0.07], [0, 0, 30], "charred", { k: 0 });
  A.obox([-3.0, GF + 0.35, 1.8], [2.0, 0.08, 0.08], [0, 0, -10], "charred", { k: 0 });
  strew(A, R, { x0: -XI + 0.3, x1: 3.5, z0: -ZI + 0.3, z1: ZI - 0.3, y: GF, n: 34, mix: MIX.burnt });
  setNav(A, {
    effect: "block",
    rooms: [room("barn", [-XI, GF, -ZI], [XI, LOFT - 0.2, ZI]), room("loft", [4.8, LOFT, -ZI], [XI, EAVE + 1.0, ZI])],
    doors: [{ a: "barn", b: "outside", p: [-2.0, GF, Z - T / 2] }, { a: "barn", b: "outside", p: [5.0, GF, Z - T / 2] }, { a: "barn", b: "loft", p: [4.27, LOFT, 0.6] }],
    entrances: [{ p: [-2.0, 0, Z + 1.2], dir: [0, 0, -1], room: "barn" }, { p: [5.0, 0, Z + 1.0], dir: [0, 0, -1], room: "barn" }],
    corners: [...corners("barn", -XI, XI, -ZI, ZI, GF, 0.6), ...corners("loft", 4.8, XI, -ZI, ZI, LOFT, 0.5)],
    firingPoints: [fire([1.8, GF, ZI - 0.4], 0, "embrasure"), fire([-6.2, GF, ZI - 0.4], 0, "embrasure"), fire([-0.5, GF, -ZI + 0.4], 180, "embrasure"), fire([3.5, GF, -ZI + 0.4], 180, "embrasure"),
                   unglazed([XI - 0.5, LOFT, 0], 90)],
    cover: [cover([-X - 0.6, 0, 2.5], 90, "high", ["left"]), cover([X + 0.6, 0, 2.5], 270, "high", ["right"]), cover([4.0, 0, -Z - 0.6], 0, "high", ["left", "right"])],
  });
  setTour(A, { pos: [-2.0, 0, 8.0], yaw: 0 }, [
    { goto: [-2.0, 6.0], note: "approach the cart doorway" }, { goto: [-1.6, 2.0], expectY: GF, note: "into the burnt barn" },
    { look: [-6, 3, -1], hold: 0.5 }, { shot: "ruin_barn_burnt_interior" },
    { goto: [2.6, -3.1] }, { goto: [4.27, -3.1], expectY: GF, note: "stair foot" },
    { goto: [4.27, 0.8], expectY: LOFT, note: "up the scorched stair" }, { goto: [5.4, 0.7], expectY: LOFT, note: "onto the surviving loft" },
    { goto: [XI - 0.6, 0.0], expectY: LOFT }, { look: [XI + 10, LOFT + 1.4, 0], hold: 0.5 }, { shot: "ruin_barn_burnt_pitching_door" },
    { goto: [5.4, 0.7] }, { goto: [4.27, 0.4] }, { goto: [4.27, -3.1], expectY: GF, note: "back down" },
    { goto: [6.0, -3.1] }, { goto: [6.0, 3.0] }, { goto: [5.0, 3.3] }, { goto: [5.0, 6.0], note: "out by the side doorway" },
  ]);
  return A;
}

// ============================================================================ R5 ruined cottage corner
// An 8 x 6 m cottage of which only the north-west corner stands (back and west walls to 3.4 m with the fireplace and
// chimney stack); the front and east walls are low stubs (cover), the rest a rubble heap with roof timbers.
export function ruinCottageCorner() {
  const A = new Asset({ id: "ruin_cottage_corner", version: 1, name: "Ruined cottage corner", category: "Ruins",
    description: "Ruin of an 8 x 6 m stone cottage: only the north-west corner stands, the back and west walls to 3.4 m with the fireplace and its chimney stack; the front and east walls are low broken stubs (good low cover), the interior a rubble heap with fallen roof timbers and slates. Enter through the gaps.",
    floors: 1, enterable: true, footprint: [8.2, 6.2], plinthDepth: 0.8, maxGrade: 1.0 });
  ruinMaterials(A);
  const R = rng(3505);
  const X = 4.0, Z = 3.0, T = 0.55, GF = 0.2;
  const floors = [[GF - 0.02, GF]];
  const bk = { axis: "x", outer: -Z, inner: -Z + T };
  const { XI, ZI } = ruinShell(A, R, { X, Z, T, GF, floors, floorMat: "hearthstone",
    front: { heights: [1.2, 0.55, 0.0, 0.75] },
    back: { heights: [3.4, 3.4, 2.0, 0.9], holes: [opening(A, { ...bk, c: -0.6, sill: GF + 1.0, w: 0.7, h: 0.9, lintel: false })] },
    east: { heights: [0.9, 0.4, 1.1] },
    west: { heights: [3.4, 3.0, 1.6], holes: [{ a0: -0.5, a1: 0.3, y0: GF + 1.0, y1: GF + 2.0 }] } });
  // fireplace on the back wall and its chimney stack
  A.box([-3.3, GF, -ZI - 0.02], [-3.0, GF + 1.1, -ZI + 0.45], "stone_dressed"); A.box([-1.98, GF, -ZI - 0.02], [-1.68, GF + 1.1, -ZI + 0.45], "stone_dressed");
  A.box([-3.38, GF + 1.1, -ZI - 0.02], [-1.53, GF + 1.32, -ZI + 0.55], "oak_x");
  A.box([-2.99, GF, -ZI - 0.02], [-1.99, GF + 1.1, -ZI + 0.015], "soot");
  A.box([-3.1, GF + 1.32, -ZI - 0.02], [-1.9, 3.25, -ZI + 0.015], "plaster_smoke");
  A.box([-3.15, 3.38, -Z - 0.05], [-1.85, 5.1, -ZI + 0.2], "stone_dressed");
  // fallen roof: rafters leaning from the back wall onto the heap, slates, a purlin
  heap(A, R, { cx: 1.0, cz: 0.5, rx: 1.7, rz: 1.4, h: 0.9, y: GF - 0.02, mix: MIX.house, clip: [-XI - 0.02, XI + 0.02, -ZI - 0.02, ZI + 0.02] });
  heap(A, R, { cx: 2.6, cz: Z + 0.4, rx: 1.4, rz: 0.7, h: 0.4, mix: MIX.stone });
  for (const [x, ang] of [[-1.2, 38], [-0.2, 31], [0.6, 25], [1.5, 35]]) A.obox([x, 1.55, -1.25], [0.1, 0.14, 3.6], [ang, (R() - 0.5) * 14, (R() - 0.5) * 8], "charred", { k: 0 });
  A.obox([-0.6, 0.95, 0.4], [4.4, 0.16, 0.14], [0, 6, 7], "oak_x", { k: 0 });
  for (let k = 0; k < 14; k++) chunk(A, [-3.2 + R() * 6.0, GF + 0.3 + R() * 0.6, -1.8 + R() * 3.4], [0.3, 0.014, 0.2], [R() * 60 - 30, R() * 360, R() * 60 - 30], "slate");
  strew(A, R, { x0: -XI + 0.3, x1: XI - 0.3, z0: -ZI + 0.3, z1: ZI - 0.3, y: GF, n: 16, mix: MIX.stone });
  strew(A, R, { x0: -X - 1.0, x1: X + 1.5, z0: Z + 0.3, z1: Z + 1.6, y: 0, n: 10, mix: MIX.stone });
  setNav(A, {
    effect: "block",
    rooms: [room("interior", [-XI, GF, -ZI], [XI, GF + 3.0, ZI])],
    doors: [{ a: "interior", b: "outside", p: [1.0, GF, Z - T / 2] }, { a: "interior", b: "outside", p: [X - T / 2, GF, 0] }],
    entrances: [{ p: [1.0, 0, Z + 1.3], dir: [0, 0, -1], room: "interior" }, { p: [X + 1.2, 0, 0], dir: [-1, 0, 0], room: "interior" }],
    corners: corners("interior", -XI, XI, -ZI, ZI, GF, 0.5),
    firingPoints: [fire([-1.0, GF, ZI - 0.3], 0, "firestep"), fire([XI - 0.3, GF, -1.6], 90, "firestep")],
    cover: [cover([-2.6, GF, ZI - 0.4], 0, "low", ["over"]), cover([XI - 0.4, GF, 1.9], 90, "low", ["over"]), cover([-X - 0.6, 0, -1.8], 90, "high", ["left", "right"]), cover([-2.4, 0, -Z - 0.6], 0, "high", ["left", "right"])],
  });
  setTour(A, { pos: [1.0, 0, 6.5], yaw: 0 }, [
    { goto: [1.0, 3.8], note: "approach the gap in the front wall" }, { goto: [0.9, 2.4], expectY: GF, note: "into the ruin" },
    { goto: [-2.4, 0.6], expectY: GF }, { look: [-2.5, 1.0, -ZI], hold: 0.5 }, { shot: "ruin_cottage_corner_hearth" },
    { goto: [-2.6, 2.0], expectY: GF, note: "behind the low front wall" },
    { goto: [1.0, 0.5], expectY: GF + 0.88, note: "up on the rubble" },
    { goto: [1.0, 2.4] }, { goto: [1.0, 4.5], note: "back out through the gap" },
  ]);
  return A;
}

// ============================================================================ R6 bombed townhouse, facade only
// The street front of a three-storey stone townhouse standing alone (9 m wide, 10 m high, window openings on three
// floors, the door and shop window) with short side returns; behind it the whole house lies in a rubble field, shored
// from behind by raking timbers.
export function ruinTownhouseFacade() {
  const A = new Asset({ id: "ruin_townhouse_facade", version: 1, name: "Bombed townhouse (facade only)", category: "Ruins",
    description: "The street facade of a three-storey dressed-stone townhouse standing alone, 9 m wide and up to 10.6 m high: string courses, empty windows on three floors, the front door and the shop window. Short side returns carry joist pockets and a hanging floor fragment; behind, the house lies in a climbable rubble field, the facade propped by raking shores.",
    floors: 1, enterable: true, footprint: [9.2, 6.8], plinthDepth: 0.8, maxGrade: 1.0 });
  ruinMaterials(A);
  const R = rng(3606);
  const X = 4.5, T = 0.6, ZO = 0.3, ZIN = ZO - T, F1 = 3.4, F2 = 6.5, BACK = -6.2;
  const fa = { axis: "x", outer: ZO, inner: ZIN };
  const XI = X - T, y0 = 0.08;
  const holes = [door(A, { id: "street_door", ...fa, c: -2.6, floor: 0.1, w: 1.2, h: 2.6, hingeSide: 1, open: true, step: false }),
    opening(A, { ...fa, c: 1.2, sill: 0.8, w: 2.0, h: 2.0 })];
  for (const sill of [F1 + 0.6, F2 + 0.55]) for (const c of [-2.8, 0, 2.8]) holes.push(opening(A, { ...fa, c, sill, w: 1.1, h: 1.8, lintel: !(sill > F2 && c > -1 && c < 2) }));
  const floors = [[y0, 0.1], [F1, F1], [F2, F2]];
  A.box([-X - 0.05, -0.8, ZIN - 0.05], [X + 0.05, y0, ZO + 0.05], "stone_plinth");
  const H = [10.2, 8.6, 7.4, 10.6];
  rwall(A, R, { axis: "x", outer: ZO, inner: ZIN, o: [-X, X], i: [-XI, XI], y0, heights: H, holes, side: "facade", floors, stone: "stone_dressed" });
  // side returns (stubs), their plaster skins and joist pockets
  rwall(A, R, { axis: "z", outer: -X, inner: -XI, o: [-2.4, ZIN + PT], i: [-2.4, ZIN], y0, heights: [6.0, 8.8], side: "west", floors });
  rwall(A, R, { axis: "z", outer: X, inner: XI, o: [-1.8, ZIN + PT], i: [-1.8, ZIN], y0, heights: [1.2, 3.0], side: "east", floors });
  A.box([-X - 0.05, -0.8, -2.45], [-XI + 0.05, y0, ZIN - 0.05], "stone_plinth"); A.box([XI - 0.05, -0.8, -1.85], [X + 0.05, y0, ZIN - 0.05], "stone_plinth");
  // string courses (embedded) over the bays standing above them
  const bw = (2 * X) / H.length;
  for (const yc of [F1 - 0.1, F2 - 0.1]) H.forEach((h, k) => { if (h > yc + 0.4) A.box([-X + k * bw - (k === 0 ? 0.04 : 0), yc, ZO - 0.04], [-X + (k + 1) * bw + (k === H.length - 1 ? 0.04 : 0), yc + 0.2, ZO + 0.09], "stone_dressed"); });
  for (const yy of [F1, F2]) for (let x = -XI + 0.3; x < XI - 0.2; x += 0.6) if (R() < 0.7) A.box([x - 0.07, yy - 0.42, ZIN - 0.2 - R() * 0.5], [x + 0.07, yy - 0.2, ZIN + 0.05], R() < 0.4 ? "charred" : "oak_z", { k: 0 });
  A.obox([-XI + 0.9, F1 - 0.5, -1.4], [1.8, 0.05, 1.6], [0, 0, -24], "floor_x", { k: 0 });   // a floor fragment hanging off the west return
  A.box([-XI - 0.01, F1 + 0.3, -2.3], [-XI + 0.012, F2 - 0.6, -0.6], "wallpaper");
  // raking shores propping the facade from behind, their sole plates bedded in the rubble
  for (const x of [-1.5, 1.6]) {
    A.obox([x, 3.75, -2.45], [0.22, 0.22, 7.6], [-48, 0, 0], "log_z", { k: 0 });
    A.box([x - 0.3, F2 + 0.4, ZIN - 0.12], [x + 0.3, F2 + 0.7, ZIN + 0.02], "oak_x", { k: 0 });
  }
  // the house in a rubble field behind the facade
  heap(A, R, { cx: 0.0, cz: -3.3, rx: 3.0, rz: 2.9, h: 1.8, mix: MIX.town, density: 8 });
  heap(A, R, { cx: 2.6, cz: -5.2, rx: 1.6, rz: 1.0, h: 0.6, mix: MIX.town });
  strew(A, R, { x0: -X, x1: X, z0: ZO + 0.3, z1: ZO + 2.0, y: 0, n: 18, mix: MIX.town });
  setNav(A, {
    effect: "block",
    rooms: [room("rubble", [-XI, 0, BACK], [XI, 3.0, ZIN])],
    doors: [{ a: "rubble", b: "outside", p: [-2.6, 0, (ZO + ZIN) / 2] }, { a: "rubble", b: "outside", p: [1.2, 0.8, (ZO + ZIN) / 2] }],
    entrances: [{ p: [-2.6, 0, ZO + 1.3], dir: [0, 0, -1], room: "rubble" }, { p: [X + 1.2, 0, -3.5], dir: [-1, 0, 0], room: "rubble" }, { p: [-X - 1.2, 0, -3.5], dir: [1, 0, 0], room: "rubble" }],
    corners: [...corners("rubble", -XI, XI, -4.0, ZIN, 0, 0.5)],
    firingPoints: [],
    cover: [cover([-3.6, 0, ZO + 0.6], 180, "high", ["left", "right"]), cover([3.6, 0, ZO + 0.6], 180, "high", ["left", "right"]), cover([-3.6, 0, ZIN - 0.5], 0, "high", ["right"])],
  });
  setTour(A, { pos: [-2.6, 0, 5.5], yaw: 0 }, [
    { goto: [-2.7, 1.6], note: "approach the street door" }, { goto: [-2.7, -0.9], expectY: 0, note: "through the doorway" },
    { goto: [0.0, -3.3], expectY: 1.78, note: "up the rubble field" },
    { look: [0, 6, 0.3], hold: 0.6 }, { shot: "ruin_townhouse_facade_behind" },
    { goto: [-3.9, -3.3] }, { goto: [-6.0, -3.3], note: "off the rubble round the west side" },
  ]);
  return A;
}

// ============================================================================ rubble pieces
function rubbleAsset(id, name, desc, footprint) {
  const A = new Asset({ id, version: 1, name, category: "Rubble", description: desc, floors: 0, enterable: false, footprint, plinthDepth: 0, maxGrade: 1.5, noAutoPanels: true });
  ruinMaterials(A);
  return A;
}
// a walkable pile: S / M / L in brick or stone
function pile(kind, size) {
  const S = { s: { rx: 0.85, rz: 0.7, h: 0.45, nav: "none" }, m: { rx: 1.6, rz: 1.3, h: 1.0, nav: "carve" }, l: { rx: 2.6, rz: 2.2, h: 1.8, nav: "carve" } }[size];
  const label = { s: "small", m: "medium", l: "large" }[size];
  const id = `rub_${kind}_pile_${size}`;
  const A = rubbleAsset(id, `${kind === "brick" ? "Brick" : "Stone"} rubble pile (${label})`,
    `A ${label} pile of ${kind === "brick" ? "broken brickwork, mortared brick clumps and plaster" : "rubble stone, dressed blocks and mortar"}, ${(2 * S.rx).toFixed(1)} x ${(2 * S.rz).toFixed(1)} m and ${S.h.toFixed(2)} m high. Walkable: the mound climbs in shallow rounded steps${S.h >= 1 ? "; " + (S.h >= 1.5 ? "high" : "low") + " cover" : ""}.`,
    [r3(2 * S.rx + 0.3), r3(2 * S.rz + 0.3)]);
  const R = rng((kind === "brick" ? 4100 : 4200) + S.h * 100);
  heap(A, R, { cx: 0, cz: 0, rx: S.rx, rz: S.rz, h: S.h, mix: MIX[kind], density: 16, core: kind === "brick" ? "brick_rubble" : "rubble_core" });
  const navC = S.h >= 1 ? [cover([-S.rx - 0.5, 0, 0], 90, S.h >= 1.5 ? "high" : "low", ["over", "left", "right"]), cover([S.rx + 0.5, 0, 0], 270, S.h >= 1.5 ? "high" : "low", ["over", "left", "right"])] : [];
  setNav(A, S.nav === "none" ? { effect: "none" } : { effect: "carve", cover: navC });
  setTour(A, { pos: [-S.rx - 2.5, 0, 0.1], yaw: 90 }, [
    { goto: [-S.rx - 0.8, 0.1], note: "walk up to the pile" }, { goto: [0, 0.05], expectY: S.h, note: "on top of the pile" },
    { goto: [S.rx + 1.0, 0.05], expectY: 0, note: "down the far side" },
  ]);
  return A;
}
export function rubBeamTileHeap() {
  const A = rubbleAsset("rub_beam_tile_heap", "Beam and tile heap", "A fallen roof: charred and sound roof timbers criss-crossed over a heap of broken clay tiles and slates, with a rafter section still carrying its tiles. 4 x 3 m, 0.9 m high; walkable, low cover.", [4.3, 3.3]);
  const R = rng(4301);
  heap(A, R, { cx: 0, cz: 0, rx: 2.0, rz: 1.5, h: 0.9, mix: MIX.roof, density: 11, core: "rubble_core" });
  for (const [x, z, yaw, tilt, L] of [[-0.6, 0.3, 20, 14, 3.6], [0.7, -0.4, -35, 10, 3.2], [0.2, 0.8, 85, 6, 2.8], [-1.0, -0.7, 110, 18, 2.4]]) A.obox([x, 0.75 + Math.sin(tilt * D2R) * 0.3, z], [0.15, 0.2, L], [tilt, yaw, 0], R() < 0.5 ? "charred" : "oak_z", { k: 0 });
  // rafter section with battens and tiles, lying on the heap's flank
  const rs = (c, s, r, m) => A.obox(c, s, r, m, { k: 0 });
  for (const dx of [-0.45, 0.45]) rs([1.1 + dx, 0.55, 0.6], [0.09, 0.12, 1.8], [-24, 30, 0], "oak_z");
  for (let k = 0; k < 4; k++) rs([1.1 + Math.sin(30 * D2R) * (k * 0.4 - 0.6), 0.36 + k * 0.14, 0.6 + Math.cos(30 * D2R) * (k * 0.4 - 0.6)], [1.2, 0.05, 0.06], [-24, 30, 0], "oak_x");
  for (let k = 0; k < 6; k++) rs([1.1 + Math.sin(30 * D2R) * (k * 0.26 - 0.65), 0.42 + k * 0.1, 0.6 + Math.cos(30 * D2R) * (k * 0.26 - 0.65)], [1.1, 0.02, 0.3], [-24, 30, 0], "terracotta");
  setNav(A, { effect: "carve", cover: [cover([0, 0, -2.0], 0, "low", ["over"]), cover([0, 0, 2.0], 180, "low", ["over"])] });
  setTour(A, { pos: [-4.5, 0, 0], yaw: 90 }, [{ goto: [-2.8, 0.0] }, { goto: [0.0, 0.0], expectY: 0.9, note: "over the heap" }, { goto: [3.0, 0.0], expectY: 0 }]);
  return A;
}
export function rubCollapsedWall() {
  const A = new Asset({ id: "rub_collapsed_wall", version: 1, name: "Collapsed wall section", category: "Rubble",
    description: "A 6 m stretch of rubble-stone wall standing 0.9-2.6 m (each standing bay still takes damage), its fallen upper courses lying as broken slabs and a rubble spill on the +Z side. Walkable over the spill; high cover behind the tall end, low cover elsewhere.",
    floors: 0, enterable: false, footprint: [6.6, 4.0], plinthDepth: 0.4, maxGrade: 1.2 });
  ruinMaterials(A);
  const R = rng(4401);
  const L = 3.0, T = 0.55;
  A.box([-L - 0.05, -0.5, -T / 2 - 0.05], [L + 0.05, 0.02, T / 2 + 0.05], "stone_plinth");
  rwall(A, R, { axis: "x", outer: T / 2, inner: -T / 2, o: [-L, L], i: [-L, L], y0: 0.02, heights: [2.6, 1.7, 0.9, 1.6], side: "wall", floors: [[0.02, 0]], plaster: "stone" });
  for (const [x, z, yaw, tilt, s] of [[1.4, 2.6, 8, 12, [1.3, 0.45, 0.9]], [-1.0, 2.75, -12, 7, [1.0, 0.4, 0.8]], [2.6, 2.4, 20, 18, [0.8, 0.4, 0.6]]]) A.obox([x, s[1] * 0.5 + 0.1, z], s, [tilt, yaw, 0], "stone");
  heap(A, R, { cx: 0.4, cz: 1.4, rx: 2.6, rz: 0.75, h: 0.5, mix: MIX.stone, density: 9 });
  strew(A, R, { x0: -L, x1: L, z0: -1.4, z1: -0.4, y: 0, n: 10, mix: MIX.stone });
  setNav(A, { effect: "carve", cover: [cover([-2.2, 0, -0.9], 0, "high", ["left"]), cover([0.0, 0, -0.9], 0, "low", ["over"]), cover([2.2, 0, -0.9], 0, "low", ["over", "right"])] });
  setTour(A, { pos: [-5.0, 0, 1.4], yaw: 90 }, [
    { goto: [-3.6, 1.4], note: "up to the spill" }, { goto: [0.4, 1.4], expectY: 0.5, note: "on the rubble spill" },
    { goto: [-3.8, 1.4] }, { goto: [-3.8, -1.2] }, { goto: [-2.2, -1.0], expectY: 0, note: "behind the standing end" },
  ]);
  return A;
}
export function rubSpillStrip() {
  const A = rubbleAsset("rub_spill_strip", "Rubble spill strip (8 m)", "A low strip of spilled rubble 8 m long and 1.6 m wide, 0.4 m high, tapering at both ends so strips join end to end along local X (snap sockets at x = -4 and +4, kind \"rubble\"). Walkable.", [8.0, 1.8]);
  const R = rng(4501);
  heap(A, R, { cx: 0, cz: 0, rx: 4.0, rz: 0.8, h: 0.4, step: 0.2, ft: 0.35, mix: MIX.house, density: 9 });
  A.meta.snap = [{ p: [-4, 0, 0], dir: [-1, 0, 0], kind: "rubble" }, { p: [4, 0, 0], dir: [1, 0, 0], kind: "rubble" }];
  setNav(A, { effect: "none" });
  setTour(A, { pos: [0.3, 0, -3.0], yaw: 0 }, [{ goto: [0.3, -1.5] }, { goto: [0.3, 0.0], expectY: 0.4, note: "across the strip" }, { goto: [0.3, 2.0], expectY: 0 }]);
  return A;
}
export function rubDebrisPatch() {
  const A = rubbleAsset("rub_debris_patch", "Scattered debris patch", "A 6 x 6 m patch of scattered debris: stones, brick clumps, slates and tiles, plaster flakes, splintered timber and a few small heaps. Walk-through dressing around ruins.", [6.0, 6.0]);
  const R = rng(4601);
  for (const [x, z, r] of [[-1.6, 1.2, 0.55], [1.4, -1.5, 0.5], [1.9, 1.8, 0.4]]) heap(A, R, { cx: x, cz: z, rx: r, rz: r * 0.8, h: 0.22, step: 0.22, mix: MIX.house, density: 12 });
  strew(A, R, { x0: -3, x1: 3, z0: -3, z1: 3, y: 0, n: 70, mix: [...MIX.house, ["brick", "brick", 2], ["terracotta", "tile", 2]] });
  setNav(A, { effect: "none" });
  setTour(A, { pos: [-4.5, 0, 0.2], yaw: 90 }, [{ goto: [0.0, 0.2], note: "through the debris" }, { goto: [4.5, 0.2] }]);
  return A;
}

export function ruinAssets() {
  return [ruinHouseShelled(), ruinFarmhouseGutted(), ruinShopRow(), ruinBarnBurnt(), ruinCottageCorner(), ruinTownhouseFacade(),
    pile("brick", "s"), pile("brick", "m"), pile("brick", "l"), pile("stone", "s"), pile("stone", "m"), pile("stone", "l"),
    rubBeamTileHeap(), rubCollapsedWall(), rubSpillStrip(), rubDebrisPatch()].map((A) => {
    debrisDetail(A);       // D1: chunky chamfered debris, splintered timber ends (appended, own RNG)
    A.meta.version += 1;   // D1 model pass: every ruin / rubble asset was version 1
    return A;
  });
}
