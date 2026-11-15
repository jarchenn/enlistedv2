// AS1 ruin kit: the "abandoned" variant of a modern asset (an abandoned, overgrown town: docs/modern-assets-brief.md#ruined).
// ruinify(json, opts) works on an asset's JSON (Asset.json()), so every catalog gets the same treatment:
//   - materials weathered: render / plaster -> flaking and stained render, concrete -> stained / wet concrete, brick ->
//     worn brick, steel -> rust, corrugated -> rusted corrugated, asphalt -> worn / patched, wood -> weathered boards,
//     fabric -> faded canvas; every albedo desaturated toward a warm grey and darkened, roughness up, metal down
//   - lamps dead (emissive off), signs faded
//   - glass: about half the panes gone (seeded), the rest dirty; some ground-floor openings boarded (no collider)
//   - ivy on some facades (the engine's ivy block), rubble heaps against the walls
//   - "ruin": true (src/interior.cpp: abandoned furnishing: heavy wear, no lamps, debris, litter, vines, emptied shelves)
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
  if (/brick/.test(n + tex) || pat === "brick" || pat === "brickwork") t = pick(["brick_worn", "brick_damaged", "brick_worn"]);
  else if (/render|plaster|stucco/.test(n + tex) || ["plaster", "limewash", "interior"].includes(pat)) t = pick(["plaster_flaking", "render_stained", "plaster_flaking_b", "render_aged"]);
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
      if (r < 0.5) {   // gone (a few leave a jagged shard along the bottom of the frame)
        if (r < 0.12) { const s = [...p.s]; const thin = s.indexOf(Math.min(...s)); const up = 1; const h = s[up] * 0.18; const c = [...p.c]; c[up] = p.c[up] - s[up] / 2 + h / 2; s[up] = h; keep.push({ ...p, c, s, k: 0 }); }
        if (p.c[1] < 2.6 && rnd(seed, i * 7 + 5) < 0.35) boards.push(p);
        return;
      }
    }
    keep.push(p);
  });
  out.parts = keep;
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
    const heaps = 2 + Math.floor(rnd(seed, 91) * 3);
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
  // ivy on some facades (src/ivy.cpp: "auto" grows over the walls)
  // IV1: old, often overgrown growth; type and pattern left to "auto", which varies them per facade (src/ivy.cpp)
  if (j.enterable && !j.ivy && rnd(seed, 77) < (opts.ivy ?? 0.55)) out.ivy = { auto: true, density: R3(0.35 + rnd(seed, 78) * 0.6), age: R3(0.6 + rnd(seed, 79) * 0.4), overgrown: rnd(seed, 80) < 0.35 };
  // nav, furniture, interactables, panels stay; the line carrier pieces keep their line block
  return out;
}

// an Asset-like wrapper for build_mpu_assets.mjs: { json() } of the ruined variant
export function ruinVariant(A, opts) { return { json: () => ruinify(A.json(), opts) }; }

// the assets that get an abandoned variant: enterable buildings, road / sidewalk bays (the "_ruin" line types), lots
export const RUIN_PIECES = /^mod_(road_|sidewalk_bay|parking_|crosswalk)/;
export function ruinVariants(assets, opts) {
  return assets.filter((A) => { const j = A.json(); return (j.enterable && !/^mod_(nt_)?(kiosk|covered_passage|compound_gate)/.test(j.id)) || RUIN_PIECES.test(j.id); }).map((A) => ruinVariant(A, opts));
}
