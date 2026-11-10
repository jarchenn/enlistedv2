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
