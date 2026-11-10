// D-day (D1): localized battle smoke and fire, made from the campfire asset (tools/gen_env_assets.py): a burning
// debris pile beside a wreck (flames plus a smoke column) and a smouldering impact (a smoke column, embers).
// Light fx "smoke_column" is src/lights.cpp. Run: node tools/gen_dday_fx.mjs
import fs from "node:fs";
const dir = new URL("../content/assets/", import.meta.url);
const src = JSON.parse(fs.readFileSync(new URL("fire_campfire.json", dir), "utf8"));
const scaled = (k) => src.parts.map((p) => ({ ...p, c: p.c.map((v, i) => +(v * (i === 1 ? k * 0.8 : k)).toFixed(4)), s: p.s.map((v, i) => +(v * (i === 1 ? k * 0.8 : k)).toFixed(4)) }));
function write(id, name, description, k, lights) {
  const A = { ...src, id, version: 1, name, category: "Lighting", description, footprint: [+(1.2 * k).toFixed(2), +(1.2 * k).toFixed(2)], parts: scaled(k), light: lights };
  A.prop = { ...src.prop, bounds: src.prop.bounds.map((b) => b.map((v) => +(v * k).toFixed(4))) };
  fs.writeFileSync(new URL(id + ".json", dir), JSON.stringify(A));
}
const glow = { color: [1, 0.45, 0.15], flicker: 0.85, shadow: 0, group: "fire", use: "fire" };
write("fx_wreck_fire", "Burning wreckage", "Burning debris beside a wreck: charred timbers in a ring of stones, flames and a dark smoke column drifting downwind (cosmetic).", 1.8,
  [{ ...glow, intensity: 4.0, radius: 14.0, offset: [0, 0.9, 0], fx: "flame_smoke", fxSize: 0.9 }, { ...glow, intensity: 0.0, radius: 1.0, offset: [0, 2.2, 0], fx: "smoke_column", fxSize: 1.6 }]);
write("fx_impact_smoke", "Smouldering impact", "A smouldering shell impact: scorched debris and embers under a thin column of dark smoke (cosmetic).", 1.3,
  [{ ...glow, intensity: 1.2, radius: 6.0, offset: [0, 0.4, 0], fx: "flame", fxSize: 0.25 }, { ...glow, intensity: 0.0, radius: 1.0, offset: [0, 1.0, 0], fx: "smoke_column", fxSize: 1.1 }]);
console.log("fx_wreck_fire, fx_impact_smoke written");

// D-day: far forest silhouettes. A treeline segment (32 x 14 m) of round crowns in two tiers over a shaded understory band,
// no collision, category "Scenery" (not decor-culled: content/settings/maps.json), so the forest beyond the culled decor
// trees still closes the horizon at about 300 tris a segment. Three seeded variants.
function rnd(seed) { let s = seed >>> 0; return () => ((s = (s * 1664525 + 1013904223) >>> 0) / 4294967296); }
const r4 = (v) => +v.toFixed(3);
for (const [k, seed] of [["a", 11], ["b", 23], ["c", 37]]) {
  const R = rnd(seed), parts = [];
  parts.push({ t: "b", c: [0, 1.5, 0], s: [30, 3.0, 9], m: "understory", k: 0 });
  for (let i = 0; i < 20; i++) {   // round crowns (rounded boxes as near-spheres), two tiers, overlapping, of varied size and tone
    const r = 3 + R() * 2.4, top = i % 3 === 0, x = -14 + R() * 28, z = -5 + R() * 10, y = (top ? 10 : 6) + R() * 4;
    parts.push({ t: "r", c: [r4(x), r4(y), r4(z)], s: [r4(2 * r), r4(1.75 * r), r4(2 * r)], rad: r4(0.86 * r), m: ["canopy_dark", "canopy_mid", "canopy_light"][i % 3], k: 0 });
  }
  for (let i = 0; i < 6; i++) {   // lower, lighter crowns along the woodland edge
    const r = 1.8 + R() * 1.2;
    parts.push({ t: "r", c: [r4(-14 + R() * 28), r4(1.5 + r), r4(5.5 + R() * 1.5)], s: [r4(2 * r), r4(1.7 * r), r4(2 * r)], rad: r4(0.84 * r), m: "canopy_light", k: 0 });
  }
  const mat = (name, albedo) => ({ name, albedo, roughness: 0.97, pattern: "plain", metallic: 0, grain: 30 });
  const A = { id: "scn_treeline_" + k, version: 1, name: "Far treeline " + k.toUpperCase(), category: "Scenery",
    description: "A distant woodland edge (32 x 14 m): rounded canopy masses over a shaded understory band, for horizons beyond the playable area (no collision, never culled; cheap far forest).",
    floors: 0, enterable: false, footprint: [32, 14], plinthDepth: 0, maxGrade: 3, noAutoPanels: true, furniture: [], interactables: [], panels: [],
    materials: [mat("canopy_dark", [0.075, 0.105, 0.05]), mat("canopy_mid", [0.1, 0.135, 0.065]), mat("canopy_light", [0.125, 0.16, 0.08]), mat("understory", [0.05, 0.055, 0.035])],
    parts, nav: { effect: "none" } };
  fs.writeFileSync(new URL(A.id + ".json", dir), JSON.stringify(A));
}
console.log("scn_treeline_a/b/c written");
