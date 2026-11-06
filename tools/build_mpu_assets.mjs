// W4 (MP & Creative update, SPEC 3.6): writes ONLY the new structure assets into content/assets/ and backfills the
// "nav" block on the existing non-bunker catalogue assets (docs/assets-mpu.md).
// Run: node tools/build_mpu_assets.mjs        (never run tools/build_assets.mjs for these; it rewrites everything)
//      node tools/build_mpu_assets.mjs --check  (writes nothing; exits 1 if a file would change)
// The bunkers (bunker_*.json, gun_mg42_embrasure.json) are W2's and are never touched here.
// Backfill: the nav block is spliced in as the file's last key, leaving every other byte of the file as its own
// generator wrote it (build_assets.mjs, gen_env_assets.py). gen_env_assets.py writes the same nav for its own assets,
// so re-running it keeps them; after re-running build_assets.mjs, run this script again.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { atlanticAssets } from "./catalog_atlantic.mjs";
import { beachAssets } from "./catalog_beach.mjs";
import { bocageAssets } from "./catalog_bocage.mjs";
import { villageAssets } from "./catalog_village2.mjs";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const out = path.join(root, "content", "assets");
const check = process.argv.includes("--check");
let changed = 0;
function put(file, text) {
  const p = path.join(out, file), old = fs.existsSync(p) ? fs.readFileSync(p, "utf8") : null;
  if (old === text) return;
  changed++;
  if (check) { console.log("would change", file); return; }
  fs.writeFileSync(p, text);
}

// ---------------------------------------------------------------- new assets
const ids = new Set();
for (const A of [...atlanticAssets(), ...beachAssets(), ...bocageAssets(), ...villageAssets()]) {
  const j = A.json();
  if (ids.has(j.id)) throw new Error("duplicate asset id " + j.id);
  ids.add(j.id);
  if (!j.nav) throw new Error(j.id + ": no nav block");
  put(j.id + ".json", JSON.stringify(j));
  if (!check) console.log(j.id.padEnd(28), String(j.parts.length).padStart(5), "parts", String(j.furniture.length).padStart(3), "furniture", String(j.interactables.length).padStart(3), "interactables", j.terrainCut ? "cut" : "");
}

// ---------------------------------------------------------------- nav backfill on the existing catalogue
const r3 = (v) => Math.round(v * 1000) / 1000;
// enterable buildings from build_assets.mjs / catalog_small / catalog_large: rooms per floor from the footprint, doors and
// entrances from the door interactables, window firing points from the casements, cover at the four outside corners
function houseNav(j) {
  const fx = j.footprint[0] / 2, fz = j.footprint[1] / 2, floors = Math.max(1, j.floors || 1);
  const ys = [0.3, 3.3, 6.3].slice(0, floors);
  const rooms = ys.map((y, i) => ({ id: "floor" + i, min: [r3(-fx + 0.6), y, r3(-fz + 0.6)], max: [r3(fx - 0.6), r3(y + 2.8), r3(fz - 0.6)] }));
  const floorOf = (y) => { let k = 0; ys.forEach((v, i) => { if (y >= v - 0.5) k = i; }); return k; };
  const doors = [], entrances = [], firingPoints = [];
  for (const it of j.interactables || []) {
    const ap = it.aperture; if (!ap) continue;
    const c = [0, 1, 2].map((i) => (ap.min[i] + ap.max[i]) / 2);
    const exterior = Math.abs(c[0]) > fx - 0.9 || Math.abs(c[2]) > fz - 0.9;
    const ax = Math.abs(c[0]) / fx > Math.abs(c[2]) / fz ? 0 : 2, sgn = Math.sign(c[ax]) || 1;
    const inward = ax === 0 ? [-sgn, 0, 0] : [0, 0, -sgn];
    const fl = floorOf(ap.min[1]);
    if (it.kind === "door" && exterior && fl === 0) {
      const p = [r3(c[0]), r3(ap.min[1]), r3(c[2])];
      doors.push({ a: "floor0", b: "outside", p });
      entrances.push({ p: [r3(c[0] - inward[0] * 1.2), r3(ap.min[1]), r3(c[2] - inward[2] * 1.2)], dir: inward, room: "floor0" });
    }
    if (it.kind === "window" && exterior) {
      const yaw = ax === 0 ? (sgn > 0 ? 90 : 270) : (sgn > 0 ? 0 : 180);
      firingPoints.push({ p: [r3(c[0] + inward[0] * 0.6), ys[fl], r3(c[2] + inward[2] * 0.6)], yaw, kind: "window" });
    }
  }
  const corners = [];
  rooms.forEach((r) => { for (const [x, z] of [[r.min[0] + 0.5, r.min[2] + 0.5], [r.max[0] - 0.5, r.min[2] + 0.5], [r.max[0] - 0.5, r.max[2] - 0.5], [r.min[0] + 0.5, r.max[2] - 0.5]]) corners.push({ p: [r3(x), r.min[1], r3(z)], room: r.id }); });
  const cover = [[1, 1, 0], [-1, 1, 0], [1, -1, 180], [-1, -1, 180]].map(([sx, sz, yaw]) => ({ p: [r3(sx * (fx + 0.7)), 0, r3(sz * (fz - 0.6))], yaw: sx > 0 ? 270 : 90, height: "high", peek: [sz > 0 ? "right" : "left"] }));
  void floorOf;
  return { effect: "block", rooms, doors, entrances, corners, firingPoints, cover, derived: "auto (tools/build_mpu_assets.mjs houseNav)" };
}
// a run of cover points along local X behind a wall / bank of length L, facing +Z (and -Z when both)
const lineCover = (L, z, height, both = true) => {
  const pts = [];
  for (let x = -L / 2 + 0.6; x <= L / 2 - 0.5; x += Math.max(1.2, (L - 1.2) / Math.max(1, Math.round(L / 2.4)))) {
    pts.push({ p: [r3(x), 0, r3(-z)], yaw: 0, height, peek: ["over"] });
    if (both) pts.push({ p: [r3(x), 0, r3(z)], yaw: 180, height, peek: ["over"] });
  }
  return pts;
};
const BACKFILL = {
  // houses and outbuildings (enterable)
  house_farmhouse_2story: "house", house_stone_cottage: "house", large_municipal_building: "house", large_stone_warehouse: "house",
  large_village_inn: "house", large_village_schoolhouse: "house", small_carpenter_workshop: "house", small_roadside_office: "house",
  small_stone_dairy: "house", small_storehouse: "house",
  // vegetation: hedgerow banks block; trees carve (trunk); shrubs are brush
  hedgerow_bocage: (j) => ({ effect: "block", cover: lineCover(j.footprint[0], 1.1, "high") }),
  tree_apple: { effect: "carve" }, tree_ash: { effect: "carve" }, tree_oak: { effect: "carve" }, tree_poplar: { effect: "carve" },
  shrub_bramble: { effect: "area", area: "brush" }, shrub_hazel: { effect: "area", area: "brush" },
  // defences: wire is forbidden ground; the hedgehog and sandbags carve and give cover
  wire_concertina: { effect: "area", area: "wire" }, wire_fence: { effect: "area", area: "wire" }, wire_knife_rest: { effect: "area", area: "wire" },
  def_tank_trap: { effect: "carve", cover: [{ p: [0, 0, -1.3], yaw: 0, height: "low", peek: ["left", "right"] }] },
  def_sandbag_straight: (j) => ({ effect: "carve", cover: lineCover(j.footprint[0], 0.9, "low") }),
  def_sandbag_curved: { effect: "carve", cover: [{ p: [0, 0, -0.9], yaw: 0, height: "low", peek: ["over"] }] },
  // field dressing
  stone_wall_low: (j) => ({ effect: "carve", cover: lineCover(j.footprint[0], 0.75, "low") }),
  fence_post_wire: { effect: "carve" }, field_gate: { effect: "carve" }, utility_pole: { effect: "carve" },
  // paths are road surface
  path_concrete: { effect: "area", area: "road" }, path_dirt: { effect: "area", area: "road" }, path_gravel: { effect: "area", area: "road" },
  // stations, racks, crates (small dressing), vehicles (dynamic: the AI treats them as moving obstacles)
  gun_flak18: { effect: "carve", cover: [{ p: [0, 0, -3.0], yaw: 0, height: "low", peek: ["left", "right"] }] },
  gun_pak40: { effect: "carve", cover: [{ p: [0, 0, -1.2], yaw: 0, height: "low", peek: ["left", "right"] }] },
  gun_m2hb_tripod: { effect: "none" }, rack_allied: { effect: "carve" }, rack_axis: { effect: "carve" },
  crate_mixed_supply: { effect: "none" }, crate_pak40_he: { effect: "none" },
  tank_sherman_firefly: { effect: "none" }, tank_tiger_i: { effect: "none" },
};
const navTable = {};
for (const file of fs.readdirSync(out).filter((f) => f.endsWith(".json")).sort()) {
  const id = file.slice(0, -5);
  if (ids.has(id) || id.startsWith("bunker_") || id === "gun_mg42_embrasure") continue;
  const rule = BACKFILL[id];
  if (!rule) { console.warn("no nav rule for", id, "(left unchanged)"); continue; }
  let text = fs.readFileSync(path.join(out, file), "utf8");
  const j = JSON.parse(text);
  const nav = rule === "house" ? houseNav(j) : typeof rule === "function" ? rule(j) : rule;
  navTable[id] = nav;
  // splice: drop a previous nav (always the last key), then append
  const at = text.lastIndexOf(',"nav":');
  if (at >= 0 && j.nav) text = text.slice(0, at) + "}";
  text = text.slice(0, text.lastIndexOf("}")) + ',"nav":' + JSON.stringify(nav) + "}";
  put(file, text);
}
// the same table for tools/gen_env_assets.py, which writes it into its own assets' output
const tablePath = path.join(root, "tools", "nav_backfill.json"), tableText = JSON.stringify(navTable, null, 1) + "\n";
if (!fs.existsSync(tablePath) || fs.readFileSync(tablePath, "utf8") !== tableText) { changed++; if (check) console.log("would change tools/nav_backfill.json"); else fs.writeFileSync(tablePath, tableText); }
console.log(check ? (changed ? `${changed} file(s) would change` : "up to date") : `${ids.size} W4 assets written, ${changed} file(s) changed`);
if (check && changed) process.exit(1);
