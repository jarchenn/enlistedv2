// TX3: the user's wall sets (ambientCG, CC0; content/textures/textures.json "TX3" slots) spread over buildings by a
// hash of the asset id and the material name, so neighbouring buildings and a building's render / brick materials
// differ. The painted-plaster slots carry a brick substrate ("under") shown where the macro mask breaks the paint.
export const RENDER_SLOTS = ["wall_pp009", "wall_pp010", "wall_pp011", "wall_pp006", "wall_pp014", "wall_pp016", "render_lime",
  // TX4: more ambientCG painted / bare plaster (brick substrate where the paint breaks)
  "painted_plaster_018", "painted_plaster_015", "painted_plaster_013", "painted_plaster_012", "painted_plaster_007", "painted_plaster_008",
  "plaster_002", "plaster_004", "painted_bricks_002"];
export const BRICK_SLOTS = ["brick_037", "brick_042", "brick_054", "brick_059", "brick_077", "brick_082b", "brick_muted", "brick_factory",
  // TX4: coursed brick (the stone-rubble "Bricks" sets stay out of modern walls) and painted brick
  "bricks_104", "bricks_092", "bricks_051", "bricks_090", "bricks_086", "bricks_088", "bricks_023", "bricks_052", "bricks_022", "bricks_103",
  "painted_bricks_004", "painted_bricks_003"];
export const CONCRETE_SLOTS = ["wall_concrete_023", "pillar_concrete_032", "concrete_formwork",
  "concrete_013", "concrete_016", "concrete_017", "concrete_020", "concrete_029", "concrete_014"];   // TX4
export const PLANK_SLOTS = ["planks_023b", "planks_024a", "planks_024b", "planks_024c", "planks_025b", "planks_026c", "planks_027c"];

export function pick(list, key) {   // FNV-1a: stable across runs and Node versions
  let h = 2166136261;
  for (let i = 0; i < key.length; i++) { h ^= key.charCodeAt(i); h = Math.imul(h, 16777619) >>> 0; }
  return list[h % list.length];
}

// the modern catalogues' exterior wall materials (render_*, brick*, cast concrete); floors take Concrete031
const RENDER = new Set(["render_m", "render_grey", "render_dk", "render_warm", "render_w"]);
const BRICK = new Set(["brick_slip", "brick_grey", "brick"]);
export function variedWalls(A) {
  const id = (A.meta && A.meta.id) || A.id;
  if (!id) throw new Error("variedWalls: asset without an id (call it after meta())");
  for (const m of A.materials) {
    if (RENDER.has(m.name)) m.texture = pick(RENDER_SLOTS, id + ":" + m.name);
    else if (BRICK.has(m.name)) m.texture = pick(BRICK_SLOTS, id + ":" + m.name);
    else if (m.name === "concrete_m") m.texture = pick(CONCRETE_SLOTS, id + ":" + m.name);
    else if (m.name === "concrete_floor") m.texture = "floor_concrete_031";
  }
  return A;
}
