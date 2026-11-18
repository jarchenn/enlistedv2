// AS1 ruin kit: the "abandoned" variant of a modern asset (an abandoned, overgrown town: docs/modern-assets-brief.md#ruined).
// ruinify(json, opts) works on an asset's JSON (Asset.json()), so every catalog gets the same treatment:
//   - materials weathered: render / plaster -> flaking and stained render, concrete -> stained / wet concrete, brick ->
//     worn brick, steel -> rust, corrugated -> rusted corrugated, asphalt -> worn / patched, wood -> weathered boards,
//     fabric -> faded canvas; every albedo desaturated toward a warm grey and darkened, roughness up, metal down
//   - lamps dead (emissive off), signs faded
//   - glass: about half the panes gone (seeded), the rest dirty; some ground-floor openings boarded (no collider)
//   - ivy on some facades (the engine's ivy block), rubble heaps against the walls
//   - "ruin": true (src/interior.cpp: abandoned furnishing: heavy wear, no lamps, debris, litter, vines, emptied shelves)
//   - NT2b: casement glass (in the window leaves) broken out too, some ground-floor windows boarded; on the facades
//     stained and spalled patches (darker render / exposed brick), one damaged top corner (exposed brick, rebar stubs,
//     fallen chunks); on a few large multi-storey buildings one collapsed bay (wall, parapet and roof edge gone over a
//     blank stretch of wall, rubble below; doors, windows and destructible bays untouched, nothing that collides added)
// The id gets "_ruin"; the clean asset stays as it was.
const hash = (s) => { let h = 2166136261; for (const c of s) { h ^= c.charCodeAt(0); h = Math.imul(h, 16777619); } return h >>> 0; };
const rnd = (seed, k) => { let x = (seed ^ Math.imul(k + 1, 0x9E3779B1)) >>> 0; x ^= x >>> 16; x = Math.imul(x, 0x7feb352d); x ^= x >>> 15; x = Math.imul(x, 0x846ca68b); x ^= x >>> 16; return (x >>> 0) / 4294967296; };
const R3 = (v) => Math.round(v * 10000) / 10000;

function weatherMat(m, seed) {
  const n = (m.name || "").toLowerCase(), tex = (m.texture || "").toLowerCase(), pat = m.pattern || "plain";
  const pick = (arr) => arr[Math.floor(rnd(seed, hash(n)) * arr.length) % arr.length];
  const o = { ...m };
  let t = null;
  if (pat === "glass") {
    o.albedo = m.albedo.map((v) => v * 0.55 + 0.04); o.roughness = Math.min(1, (m.roughness || 0.1) + 0.35);
    return o;
  }
  if (/^(wall_pp|brick_0|road_0|planks_0|wall_concrete|pillar_concrete|floor_concrete|bark_0)/.test(tex)) return o;   // TX3: the user's sets (tools/wall_variety.mjs) are weathered already: the abandoned variant keeps them
  if (/plinth|footing|foundation|socle/.test(n)) t = pick(["footing_brick", "footing_rock", "footing_brick"]);   // NT4: exposed footing (Bricks061 / Rock022)
  else if (/brick/.test(n + tex) || pat === "brick" || pat === "brickwork") t = pick(["brick_worn", "brick_damaged", "brick_worn"]);
  else if (/render|plaster|stucco/.test(n + tex) || ["plaster", "limewash", "interior"].includes(pat)) t = pick(["render_stained", "render_aged", "render_stained"]);   // NT7: no plaster_flaking (it read as camouflage blotches at building scale)
  else if (/asphalt|tarmac/.test(n + tex)) { t = pick(["asphalt_worn", "asphalt_patched", "asphalt_light"]); o.texture = t; o.albedo = [0.34, 0.33, 0.31]; o.roughness = 0.97; return o; }   // sun-bleached, dusty
  else if (/membrane|roof_mem/.test(n + tex)) t = pick(["asphalt_worn", "asphalt_patched"]);
  else if (/corrug/.test(n + tex)) t = "corrugated_blue_rusty";
  else if (/concrete|precast|kerb|curb|paving|stone|slab|plinth|cast|block|cinder/.test(n + tex) || pat === "concrete" || pat === "bunker") t = pick(["concrete_stained", "concrete_dirty_wet", "bunker_weathered", "concrete_aged"]);
  else if (pat === "metal" || /steel|metal|alu|galv|zinc|iron|chrome|stainless|rail|frame|grille|louvre|hvac/.test(n + tex)) {
    t = pick(["steel_rust_light", "rust_heavy", "iron_worn", "steel_rust_light"]);
    o.metallic = Math.min(m.metallic || 0, 0.2);
  } else if (pat === "wood" || pat === "painted" || /wood|plank|oak|timber|deck|ply|laminate|clad/.test(n + tex)) t = /floor|laminate|deck/.test(n) ? "floor_boards_worn" : pick(["boards_weathered", "paint_wood_old", "wood_old_planks"]);
  else if (pat === "fabric" || /fabric|canvas|awning|cloth/.test(n + tex)) t = "canvas_rough";
  else if (/tile|ceramic|terrazzo|marble/.test(n + tex) || pat === "tile" || pat === "ceramic") t = null;   // keeps its pattern, gets grimy
  if (t) o.texture = t;
  // desaturate toward a warm grey, darken, rough up; signs and paint fade most
  const a = m.albedo || [0.5, 0.5, 0.5];
  const lum = 0.2126 * a[0] + 0.7152 * a[1] + 0.0722 * a[2];
  const k = /sign|stripe|awning|paint|accent|fascia|panel_acc|car_|line_/.test(n) ? 0.6 : 0.42;
  const warm = [1.04, 1.0, 0.93];
  o.albedo = a.map((v, i) => R3(Math.max(0.02, (v + (lum - v) * k) * 0.8 * warm[i])));
  o.roughness = R3(Math.min(1, (m.roughness ?? 0.6) + 0.18));
  if (o.emissive) o.emissive = [0, 0, 0];   // dead lamps, dark signs
  return o;
}

export function ruinify(j, opts = {}) {
  const seed = hash(j.id) ^ (opts.seed || 0x5eed);
  const out = JSON.parse(JSON.stringify(j));
  out.id = j.id + "_ruin";
  out.name = (j.name || j.id) + " (abandoned)";
  out.description = "Abandoned and overgrown: " + (j.description || "");
  out.ruin = true;
  for (const p of out.parts) delete p.ru;   // NT7: the ruin-only parts are ordinary parts here
  if (j.model) out.model = { ...j.model, dir: (j.model.dir || j.id) + "_ruin", tint: [0.74, 0.71, 0.66] };   // MOD2: an imported model: its own darker, dustier bake
  out.materials = j.materials.map((m) => weatherMat(m, seed));
  const glassIdx = new Set(out.materials.map((m, i) => (m.pattern === "glass" ? i : -1)).filter((i) => i >= 0));
  const matIndex = (name) => out.materials.findIndex((m) => m.name === name);
  // glass: about half the panes gone; boards over a few ground-floor openings
  if (!out.materials.some((m) => m.name === "ruin_board")) out.materials.push({ name: "ruin_board", albedo: [0.42, 0.36, 0.28], roughness: 0.9, pattern: "plain", metallic: 0, grain: 1, texture: "boards_weathered" });
  if (!out.materials.some((m) => m.name === "ruin_rubble")) out.materials.push({ name: "ruin_rubble", albedo: [0.46, 0.44, 0.4], roughness: 0.95, pattern: "concrete", metallic: 0, grain: 1, texture: "debris_brick" });
  const boardM = matIndex("ruin_board"), rubbleM = matIndex("ruin_rubble");
  const keep = [], boards = [];
  out.parts.forEach((p, i) => {
    const mi = typeof p.m === "number" ? p.m : matIndex(p.m);
    const glass = glassIdx.has(mi) || (typeof p.m === "string" && /glass/.test(p.m) && out.materials[matIndex(p.m)]?.pattern === "glass");
    const mn = typeof p.m === "number" ? out.materials[p.m]?.name || "" : p.m || "";
    if (/^(goods|pack_|stock)/.test(mn) && rnd(seed, i * 7 + 11) < 0.85) return;   // looted: the built-in shelves stand mostly empty
    if (glass && p.t === "b" && p.v !== 0) {
      const r = rnd(seed, i * 7 + 3);
      if (r < 0.62) {   // gone (a few leave a jagged shard along the bottom of the frame)
        if (r < 0.12) { const s = [...p.s]; const thin = s.indexOf(Math.min(...s)); const up = 1; const h = s[up] * 0.18; const c = [...p.c]; c[up] = p.c[up] - s[up] / 2 + h / 2; s[up] = h; keep.push({ ...p, c, s, k: 0 }); }
        if (rnd(seed, i * 7 + 5) < (p.c[1] < 2.6 ? 0.35 : 0.12)) boards.push(p);   // NT4: upper ribbon bays boarded too
        return;
      }
    }
    keep.push(p);
  });
  out.parts = keep;
  // NT2b: the casement glass lives in the window leaves: break about half of it out, board some ground-floor windows
  const boardWin = [];
  for (const [w, it] of (out.interactables || []).entries()) {
    if (it.kind === "door" || it.kind === "gate") continue;
    it.defaultOpen = true;   // NT4: ruin windows hang open (the navmesh's vault links through them assume an open leaf)
    let gone = false;
    for (const L of it.leaves || []) L.parts = L.parts.filter((p, i) => {
      const gl = typeof p.m === "string" ? out.materials[matIndex(p.m)]?.pattern === "glass" : glassIdx.has(p.m);
      if (!gl || p.v === 0 || rnd(seed, 5000 + w * 31 + i) >= 0.55) return true;
      gone = true; return false;
    });
    if (gone && it.aperture && it.aperture.min[1] < 2.6 && rnd(seed, 6000 + w) < 0.35) boardWin.push(it.aperture);
  }
  const cx0 = 0;   // the asset origin is the footprint centre
  for (const ap of boardWin) {   // planks across the outer face of the opening (the face further from the centre)
    const sx = ap.max[0] - ap.min[0], sz = ap.max[2] - ap.min[2], n = sx < sz ? 0 : 2, w = 2 - n;
    const outer = Math.abs(ap.max[n] - cx0) > Math.abs(ap.min[n] - cx0) ? ap.max[n] + 0.035 : ap.min[n] - 0.035, span = ap.max[w] - ap.min[w], hgt = ap.max[1] - ap.min[1];
    const k = 2 + Math.floor(rnd(seed, ap.min[1] * 97 + ap.min[w] * 13) * 2);
    for (let b = 0; b < k; b++) {
      const c = [0, R3(ap.min[1] + hgt * (0.2 + 0.6 * (b / Math.max(1, k - 1)))), 0], s = [0, 0.16, 0];
      c[n] = R3(outer); c[w] = R3((ap.min[w] + ap.max[w]) / 2); s[n] = 0.022; s[w] = R3(span + 0.16);
      const tilt = (rnd(seed, b * 37 + ap.min[w] * 11) - 0.5) * 12;
      out.parts.push({ t: "b", c, s, r: n === 0 ? [R3(tilt), 0, 0] : [0, 0, R3(tilt)], m: typeof out.parts[0]?.m === "number" ? boardM : "ruin_board", k: 0, lod: 1 });
    }
  }
  for (const p of boards) {   // two or three planks across the opening, a few cm proud of the glass line
    const s = p.s, thinAx = s[0] < s[2] ? 0 : 2, wAx = 2 - thinAx;
    const n = 2 + Math.floor(rnd(seed, p.c[0] * 13 + p.c[2] * 7) * 2);
    for (let b = 0; b < n; ++b) {
      const c = [...p.c], sz = [0, 0.16, 0];
      c[1] = p.c[1] - s[1] * 0.35 + (s[1] * 0.7) * (n === 1 ? 0.5 : b / (n - 1));
      c[thinAx] = p.c[thinAx] + 0.035 * (p.c[thinAx] >= 0 ? 1 : -1);
      sz[thinAx] = 0.022; sz[wAx] = s[wAx] + 0.18;
      const tilt = (rnd(seed, b * 31 + p.c[1] * 17) - 0.5) * 14;
      const r = thinAx === 0 ? [tilt, 0, 0] : [0, 0, tilt];
      out.parts.push({ t: "b", c: c.map(R3), s: sz.map(R3), r: r.map(R3), m: typeof p.m === "number" ? boardM : "ruin_board", k: 0, lod: 1 });
    }
  }
  // rubble heaps against the walls (enterable buildings and walls only), outside the door apertures
  const fp = j.footprint || [4, 4];
  if (j.enterable && fp[0] * fp[1] > 40) {
    const doors = (j.interactables || []).filter((d) => d.kind === "door" || d.kind === "gate").map((d) => d.aperture || {});
    const nearDoor = (x, z) => doors.some((d) => d.min && d.max && x > d.min[0] - 1.6 && x < d.max[0] + 1.6 && z > d.min[2] - 2.2 && z < d.max[2] + 2.2);
    const heaps = out.parts.length > 1440 ? 1 : 2 + Math.floor(rnd(seed, 91) * 3);   // NT2b: fewer heaps on assets at the part budget
    for (let h = 0; h < heaps; ++h) {
      const side = Math.floor(rnd(seed, 100 + h) * 4), u = rnd(seed, 120 + h) * 0.8 - 0.4;
      let x = side < 2 ? u * fp[0] : (side === 2 ? -1 : 1) * (fp[0] / 2 + 0.5), z = side < 2 ? (side === 0 ? -1 : 1) * (fp[1] / 2 + 0.5) : u * fp[1];
      if (nearDoor(x, z)) continue;
      for (let c = 0; c < 6; ++c) {
        const sx = 0.25 + rnd(seed, h * 50 + c) * 0.55, sy = 0.12 + rnd(seed, h * 50 + c + 9) * 0.3;
        out.parts.push({ t: "b", c: [R3(x + (rnd(seed, h * 50 + c + 1) - 0.5) * 1.6), R3(sy * 0.4), R3(z + (rnd(seed, h * 50 + c + 2) - 0.5) * 1.6)], s: [R3(sx), R3(sy), R3(sx * 0.8)],
          r: [R3((rnd(seed, c + 3) - 0.5) * 30), R3(rnd(seed, c + 4) * 180), R3((rnd(seed, c + 5) - 0.5) * 30)], m: typeof out.parts[0]?.m === "number" ? rubbleM : "ruin_rubble", k: 0, lod: 1, bv: 0.03 });
      }
    }
  }
  if (typeof out.parts[0]?.m === "string") damage(out, seed);
  // ivy on some facades (src/ivy.cpp: "auto" grows over the walls)
  // IV1: old, often overgrown growth; type and pattern left to "auto", which varies them per facade (src/ivy.cpp)
  if (j.enterable && !j.ivy && rnd(seed, 77) < (opts.ivy ?? 0.95)) out.ivy = { auto: true, density: R3(1.0 + rnd(seed, 78) * 0.8), age: R3(0.8 + rnd(seed, 79) * 0.2), overgrown: rnd(seed, 80) < 0.8, share: 0.95 };   // NT2b: nearly every ruin under heavy, mostly overgrown ivy (30-60 % of the facades)
  // nav, furniture, interactables, panels stay; the line carrier pieces keep their line block
  return out;
}

// ============================================================================ NT2b damage (ruins only)
const ovl = (a0, a1, b0, b1) => a0 < b1 - 1e-4 && a1 > b0 + 1e-4;
const bHit = (a, b) => [0, 1, 2].every((i) => ovl(a[0][i], a[1][i], b[0][i], b[1][i]));
const turned = (p) => (p.r && p.r.some((v) => Math.abs(v) > 1e-4)) || (p.q && (Math.abs(p.q[0]) > 1e-4 || Math.abs(p.q[1]) > 1e-4 || Math.abs(p.q[2]) > 1e-4));
const bbOf = (p) => { let he = p.s.map((v) => v / 2); if (turned(p)) { const e = Math.hypot(...he); he = [e, e, e]; } return [p.c.map((v, i) => v - he[i]), p.c.map((v, i) => v + he[i])]; };
const box = (mn, mx, m, o = {}) => ({ t: "b", c: [0, 1, 2].map((i) => R3((mn[i] + mx[i]) / 2)), s: [0, 1, 2].map((i) => R3(mx[i] - mn[i])), m, ...o });
const addMat = (out, name, def) => { if (!out.materials.some((m) => m.name === name)) out.materials.push({ name, metallic: 0, grain: 1, ...def }); };
// a box minus a region: up to six boxes (non-rotated parts only)
function subtract(p, R) {
  const [mn, mx] = bbOf(p), out = [], lo = [...mn], hi = [...mx];
  for (let a = 0; a < 3; a++) {
    if (R[0][a] > lo[a] + 1e-4) { const h = [...hi]; h[a] = R[0][a]; out.push([[...lo], h]); lo[a] = R[0][a]; }
    if (R[1][a] < hi[a] - 1e-4) { const l = [...lo]; l[a] = R[1][a]; out.push([l, [...hi]]); hi[a] = R[1][a]; }
  }
  const { bv, ...rest } = p;
  return out.filter(([a, b]) => [0, 1, 2].every((i) => b[i] - a[i] > 0.004)).map(([a, b]) => ({ ...rest, c: [0, 1, 2].map((i) => R3((a[i] + b[i]) / 2)), s: [0, 1, 2].map((i) => R3(b[i] - a[i])) }));
}
function damage(out, seed) {
  const fp = out.footprint || [4, 4];
  if (!out.enterable || fp[0] * fp[1] < 40) return;
  addMat(out, "ruin_brick", { albedo: [0.42, 0.3, 0.24], roughness: 0.95, pattern: "brick", texture: "brick_damaged" });
  addMat(out, "ruin_stain", { albedo: [0.3, 0.29, 0.26], roughness: 0.97, pattern: "concrete", texture: "concrete_dirty_wet" });
  addMat(out, "ruin_rebar", { albedo: [0.3, 0.17, 0.1], roughness: 0.8, pattern: "metal", texture: "rust_heavy" });
  const rnd2 = (k) => rnd(seed, 9000 + k), matOf = (n) => out.materials.find((m) => m.name === n) || {};
  const panelParts = (out.panels || []).flatMap((pn) => pn.states?.[0] || []);
  const aps = (out.interactables || []).filter((it) => it.aperture).map((it) => [it.aperture.min, it.aperture.max]);
  const panelBoxes = (out.panels || []).filter((pn) => pn.min && pn.max).map((pn) => [pn.min, pn.max]);
  // wall pieces: upright, thin, colliding boxes; the envelope they span; their outer faces on the envelope
  const walls = out.parts.concat(panelParts).filter((p) => p.t === "b" && !turned(p) && p.k !== 0 && p.v !== 0 && p.s[1] >= 1.5 && Math.min(p.s[0], p.s[2]) <= 0.6 && Math.max(p.s[0], p.s[2]) >= 1.2);
  if (walls.length < 4) return;
  const E = [[1e9, 0, 1e9], [-1e9, 0, -1e9]];
  for (const p of walls) { const [a, b] = bbOf(p); for (const i of [0, 2]) { E[0][i] = Math.min(E[0][i], a[i]); E[1][i] = Math.max(E[1][i], b[i]); } E[1][1] = Math.max(E[1][1], b[1]); }
  const faces = [];   // { n: axis of the normal, s: sign, d: plane, p }
  for (const p of walls) {
    const [a, b] = bbOf(p), n = p.s[0] < p.s[2] ? 0 : 2;
    if (Math.abs(b[n] - E[1][n]) < 0.06) faces.push({ n, s: 1, d: b[n], p, a, b });
    else if (Math.abs(a[n] - E[0][n]) < 0.06) faces.push({ n, s: -1, d: a[n], p, a, b });
  }
  if (!faces.length) return;
  const fb = (f, u0, u1, y0, y1, e0, e1) => { const t = 2 - f.n, d0 = f.d + f.s * e0, d1 = f.d + f.s * e1, mn = [0, y0, 0], mx = [0, y1, 0]; mn[f.n] = Math.min(d0, d1); mx[f.n] = Math.max(d0, d1); mn[t] = u0; mx[t] = u1; return [mn, mx]; };
  const allBB = () => out.parts.filter((p) => p.s && p.t !== "p").map(bbOf);
  const clear = (B, full) => !allBB().some((b) => bHit(b, B)) && !aps.some((b) => bHit(b, full));
  // 1. stained and spalled patches: 8 mm proud of the face, never over an opening, a sill or a sign
  const tight = out.parts.length > 1420;   // keep heavy assets near their part budget
  const full = out.parts.length > 1450;   // already at the budget: the corner and the collapse only
  const nP = full ? 0 : tight ? 3 : Math.min(12, 4 + Math.round((fp[0] + fp[1]) / 8));
  for (let k = 0, made = 0; k < nP * 4 && made < nP; k++) {
    const f = faces[Math.floor(rnd2(k * 5) * faces.length)], t = 2 - f.n;
    const w = 0.5 + rnd2(k * 5 + 1) * 1.3, h = 0.35 + rnd2(k * 5 + 2) * 0.9;
    if (f.b[t] - f.a[t] < w + 0.3 || f.b[1] - f.a[1] < h + 0.3) continue;
    const u = f.a[t] + 0.15 + rnd2(k * 5 + 3) * (f.b[t] - f.a[t] - w - 0.3), y = Math.max(0.3, f.a[1] + 0.15) + rnd2(k * 5 + 4) * Math.max(0, f.b[1] - Math.max(0.3, f.a[1] + 0.15) - h - 0.15);
    const B = fb(f, u, u + w, y, y + h, -0.01, 0.008);
    if (!clear(fb(f, u - 0.05, u + w + 0.05, y - 0.05, y + h + 0.05, 0.002, 0.03), B)) continue;
    const wm = matOf(f.p.m), brick = /brick/.test((wm.name || "") + (wm.texture || "")) || wm.pattern === "brick";
    out.parts.push(box(B[0], B[1], brick || made % 2 ? "ruin_stain" : "ruin_brick", { k: 0, lod: 1 }));
    made++;
  }
  // 2. one damaged top corner: exposed brick stepping down both faces, rebar stubs, fallen chunks at its foot
  const ci = Math.floor(rnd2(400) * 4), sx = ci & 1 ? 1 : -1, sz = ci & 2 ? 1 : -1, cx = sx > 0 ? E[1][0] : E[0][0], cz = sz > 0 ? E[1][2] : E[0][2];
  let yT = 0;
  for (const b of allBB()) if (b[0][0] < cx + 0.3 && b[1][0] > cx - 0.3 && b[0][2] < cz + 0.3 && b[1][2] > cz - 0.3 && b[1][1] < E[1][1] + 3) yT = Math.max(yT, b[1][1]);
  if (yT > 2.5) {
    for (const [len, dy] of [[1.1, 1.5], [0.6, 2.3]]) for (const ax of [0, 2]) {
      const mn = [0, yT - dy, 0], mx = [0, yT - 0.05, 0], o = 2 - ax, c = ax === 0 ? cx : cz, cc = ax === 0 ? cz : cx, so = ax === 0 ? sz : sx, sa = ax === 0 ? sx : sz;
      mn[ax] = Math.min(c - sa * len, c + sa * 0.012); mx[ax] = Math.max(c - sa * len, c + sa * 0.012);
      mn[o] = Math.min(cc, cc + so * 0.012); mx[o] = Math.max(cc, cc + so * 0.012);
      if (!aps.some((b) => bHit(b, [mn, mx]))) out.parts.push(box(mn, mx, "ruin_brick", { k: 0, lod: 1 }));
    }
    for (let k = 0; k < (tight ? 3 : 5); k++) {
      const x = cx - sx * (0.1 + rnd2(410 + k) * 0.5), z = cz - sz * (0.1 + rnd2(420 + k) * 0.5), L = 0.35 + rnd2(430 + k) * 0.4;
      out.parts.push({ t: "c", c: [R3(x), R3(yT + L / 2 - 0.1), R3(z)], s: [0.022, R3(L), 0.022], r: [R3((rnd2(440 + k) - 0.5) * 40), 0, R3((rnd2(450 + k) - 0.5) * 40)], m: "ruin_rebar", k: 0, lod: 1, seg: 6 });
    }
    for (let k = 0; k < (full ? 0 : tight ? 2 : 4); k++) { const s = 0.25 + rnd2(460 + k) * 0.35; out.parts.push({ t: "b", c: [R3(cx + sx * (0.4 + rnd2(470 + k) * 1.2)), R3(s * 0.35), R3(cz + sz * (0.4 + rnd2(480 + k) * 1.2))], s: [R3(s), R3(s * 0.6), R3(s * 0.8)], r: [R3(rnd2(490 + k) * 30), R3(rnd2(500 + k) * 180), 0], m: "ruin_brick", k: 0, lod: 1, bv: 0.03 }); }
  }
  // 3. a collapsed bay on a few large multi-storey buildings, over a stretch of wall with no opening in it
  if ((out.floors || 1) < 2 || fp[0] * fp[1] < 250 || rnd2(600) > 0.7) return;
  const H = E[1][1], W = 3.4, cands = [];
  for (const f of faces) {
    const t = 2 - f.n;
    for (let u = f.a[t] + 0.4; u + W <= f.b[t] - 0.4; u += 0.5) {
      const yCut = Math.max(3.2, H * (0.45 + rnd2(610) * 0.2));
      const Rs = [fb(f, u, u + W, yCut, H + 3, -0.7, 0.5), fb(f, u + 0.6, u + W - 0.9, yCut - 1.0, yCut + 0.1, -0.7, 0.5), fb(f, u + 0.3, u + W - 0.3, H - 0.6, H + 3, 0.3, 0.9)];
      if (Rs.some((R) => aps.some((b) => bHit(b, R)) || panelBoxes.some((b) => bHit(b, R)))) continue;
      cands.push({ f, u, Rs });
    }
  }
  if (!cands.length) return;
  const { f, u, Rs } = cands[Math.floor(rnd2(620) * cands.length)];
  let parts = out.parts;
  for (const R of Rs) parts = parts.flatMap((p) => {
    if (!p.s || p.t === "p") return [p];
    const b = bbOf(p);
    if (!bHit(b, R)) return [p];
    if (turned(p) || p.t !== "b") return p.c.every((v, i) => v > R[0][i] && v < R[1][i]) ? [] : [p];
    return subtract(p, R);
  });
  out.parts = parts;
  // rubble fanned out below the hole (no colliders: the ground stays walkable), slab fragments, hanging rebar
  const t = 2 - f.n;
  for (let k = 0; k < (full ? 4 : tight ? 7 : 12); k++) {
    const s = 0.3 + rnd2(700 + k) * 0.7, along = u + 0.2 + rnd2(710 + k) * (W - 0.4), outw = 0.2 + rnd2(720 + k) * 2.0;
    const c = [0, R3(s * 0.3 * (1 - outw / 3)), 0]; c[t] = R3(along); c[f.n] = R3(f.d + f.s * outw);
    out.parts.push({ t: "b", c, s: [R3(s), R3(s * 0.55), R3(s * 0.8)], r: [R3((rnd2(730 + k) - 0.5) * 50), R3(rnd2(740 + k) * 180), R3((rnd2(750 + k) - 0.5) * 50)], m: k % 3 ? "ruin_rubble" : "ruin_brick", k: 0, lod: k > 5 ? 1 : 0, bv: 0.03 });
  }
  for (let k = 0; k < 2; k++) { const c = [0, 0.25, 0]; c[t] = R3(u + 0.8 + k * 1.6); c[f.n] = R3(f.d + f.s * (0.8 + k * 0.7)); out.parts.push({ t: "b", c, s: [1.3, 0.2, 0.9], r: [R3(12 - k * 20), R3(30 + k * 70), R3(8)], m: "ruin_stain", k: 0 }); }
  for (let k = 0; k < 4; k++) { const c = [0, 0, 0]; c[t] = R3(u + 0.4 + k * 0.8); c[f.n] = R3(f.d - f.s * 0.15); c[1] = R3(Math.max(3.2, H * 0.45) - 0.3); out.parts.push({ t: "c", c, s: [0.02, 0.7, 0.02], r: [R3((rnd2(760 + k) - 0.5) * 30), 0, R3((rnd2(770 + k) - 0.5) * 30)], m: "ruin_rebar", k: 0, lod: 1, seg: 6 }); }
}

// an Asset-like wrapper for build_mpu_assets.mjs: { json() } of the ruined variant
export function ruinVariant(A, opts) { return { json: () => ruinify(A.json({ ruin: true }), opts) }; }

// the assets that get an abandoned variant: enterable buildings, road / sidewalk bays (the "_ruin" line types), lots
export const RUIN_PIECES = /^mod_(road_|sidewalk_bay|parking_|crosswalk)/;
export function ruinVariants(assets, opts) {
  return assets.filter((A) => { const j = A.json(); return (j.enterable && !/^mod_(nt_)?(kiosk|covered_passage|compound_gate)/.test(j.id)) || RUIN_PIECES.test(j.id); }).map((A) => ruinVariant(A, opts));
}
