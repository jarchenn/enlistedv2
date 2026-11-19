// TX3: the user's wall sets (ambientCG, CC0; content/textures/textures.json "TX3" slots) spread over buildings by a
// hash of the asset id and the material name, so neighbouring buildings and a building's render / brick materials
// differ. The painted-plaster slots carry a brick substrate ("under") shown where the macro mask breaks the paint.
// TX5: weighted by repetition (pick() hashes into the list): walls draw worn sets, chipped / peeling paint over brick,
// stained and streaked concrete, faded and whitewashed brick. Buildings differ by the kind of wear, not by clean
// colours: the tidy TX4 bricks, the plain or loud TX4 plasters and uniform painted brick stay in the library and the
// paint picker but not in these pools.
export const RENDER_SLOTS = [
  "wall_pp010", "wall_pp010", "wall_pp010", "wall_pp010",   // white paint heavily flaked to red brick
  "wall_pp014", "wall_pp014", "wall_pp016", "wall_pp016",   // grey plaster flaking in brown patches
  "painted_plaster_015", "painted_plaster_015",             // grey paint, small flakes
  "wall_pp009", "wall_pp011", "wall_pp006",                 // grey-green flakes, green-grey mossy, aged render
  "painted_plaster_008"];                                   // blue-grey paint peeling to brown (the one colour, rare)
export const BRICK_SLOTS = [
  "brick_077", "brick_077", "bricks_090", "bricks_090", "bricks_022", "bricks_022",   // stained, faded, whitewash remains
  "brick_037", "brick_042", "brick_054", "brick_059", "bricks_023", "brick_muted", "brick_factory"];
export const CONCRETE_SLOTS = [
  "pillar_concrete_032", "pillar_concrete_032",             // stained bunker concrete with mould
  "wall_concrete_023", "concrete_029", "concrete_026", "concrete_039", "concrete_formwork"];   // cracked, streaked, lichen, blotched
// TX5: buildings kept exactly as they were before TX4 (the user preferred the farmhouse's flaking white paint over brick)
const PINNED = {
  mod_nt_farmhouse: {render_warm: "wall_pp010", render_dk: "wall_pp011", render_grey: "wall_pp014", render_m: "wall_pp016",
    brick: "brick_muted", brick_grey: "brick_077", brick_slip: "brick_082b", concrete_m: "concrete_formwork"},
};
// TX5: dirt and chipping on these buildings by default (src/interior.cpp defaultWear; modern buildings are otherwise 0.2,
// the old houses 1.0; on "abandoned" maps every modern building is a ruin at 1.85 anyway)
export const WALL_WEAR = 1.0;
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
  const pin = PINNED[id.replace(/_ruin$/, "")];
  if (A.meta && A.meta.wear === undefined) A.meta.wear = WALL_WEAR;
  for (const m of A.materials) {
    if (pin && pin[m.name]) m.texture = pin[m.name];
    else if (RENDER.has(m.name)) m.texture = pick(RENDER_SLOTS, id + ":" + m.name);
    else if (BRICK.has(m.name)) m.texture = pick(BRICK_SLOTS, id + ":" + m.name);
    else if (m.name === "concrete_m") m.texture = pick(CONCRETE_SLOTS, id + ":" + m.name);
    else if (m.name === "concrete_floor") m.texture = "floor_concrete_031";
  }
  return A;
}
