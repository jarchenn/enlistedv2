// Furniture and fittings for outbuildings and bunkers. Each function adds one furniture group
// (visual parts + one bounding collider) unless collide === false.

// open shelving along X (back at zb, front toward s = ±1)
export function shelfX(A, name, x0, x1, zb, s, y, h, depth, levels, mat = "plank_x", items = null, collide = true) {
  A.beginFurniture(name);
  const zf = zb + s * depth, za = Math.min(zb, zf), zz = Math.max(zb, zf);
  for (const x of [x0, x1 - 0.04]) A.box([x, y, za], [x + 0.04, y + h, zz], mat);
  for (let i = 0; i < levels; i++) {
    const yy = y + 0.12 + (i * (h - 0.16)) / Math.max(1, levels - 1);
    A.box([x0, yy, za], [x1, yy + 0.025, zz], mat);
    if (items) items(yy + 0.025, za, zz, i);
  }
  A.endFurniture(collide);
}
// shelving along Z (back at xb, front toward s)
export function shelfZ(A, name, z0, z1, xb, s, y, h, depth, levels, mat = "plank_z", items = null, collide = true) {
  A.beginFurniture(name);
  const xf = xb + s * depth, xa = Math.min(xb, xf), xx = Math.max(xb, xf);
  for (const z of [z0, z1 - 0.04]) A.box([xa, y, z], [xx, y + h, z + 0.04], mat);
  for (let i = 0; i < levels; i++) {
    const yy = y + 0.12 + (i * (h - 0.16)) / Math.max(1, levels - 1);
    A.box([xa, yy, z0], [xx, yy + 0.025, z1], mat);
    if (items) items(yy + 0.025, xa, xx, i);
  }
  A.endFurniture(collide);
}
export function crateBox(A, x, y, z, sx, sy, sz, mat = "plank_x", rotY = 0) {
  if (rotY) { A.obox([x, y + sy / 2, z], [sx, sy, sz], [0, rotY, 0], mat); return; }
  A.box([x - sx / 2, y, z - sz / 2], [x + sx / 2, y + sy, z + sz / 2], mat);
  for (const yy of [y + 0.03, y + sy - 0.03]) A.box([x - sx / 2 - 0.008, yy - 0.03, z - sz / 2 - 0.008], [x + sx / 2 + 0.008, yy + 0.03, z + sz / 2 + 0.008], mat);
}
export function crates(A, name, list, mat = "plank_x") {
  // one collider per crate: a single bounding box would span the gaps between scattered stacks
  A.beginFurniture(name);
  for (const c of list) {
    crateBox(A, c[0], c[1], c[2], c[3], c[4], c[5], mat, c[6] || 0);
    if (c[6]) A.obox([c[0], c[1] + c[4] / 2, c[2]], [c[3], c[4], c[5]], [0, c[6], 0], mat, { v: 0 });
    else A.box([c[0] - c[3] / 2, c[1], c[2] - c[5] / 2], [c[0] + c[3] / 2, c[1] + c[4], c[2] + c[5] / 2], mat, { v: 0 });
  }
  A.endFurniture(false);
}
// barrel approximated as an octagonal prism with iron hoops
export function barrel(A, name, x, z, y, r = 0.3, h = 0.85, mat = "oak_y") {
  A.beginFurniture(name);
  const s = r * 1.66;
  A.box([x - s / 2, y, z - s / 2], [x + s / 2, y + h, z + s / 2], mat);
  A.obox([x, y + h / 2, z], [s, h, s], [0, 45, 0], mat);
  for (const f of [0.12, 0.5, 0.88]) {
    A.box([x - s / 2 - 0.006, y + h * f - 0.02, z - s / 2 - 0.006], [x + s / 2 + 0.006, y + h * f + 0.02, z + s / 2 + 0.006], "iron");
    A.obox([x, y + h * f, z], [s + 0.012, 0.04, s + 0.012], [0, 45, 0], "iron");
  }
  A.endFurniture();
}
export function sacks(A, name, list) {
  A.beginFurniture(name);
  for (const [x, y, z, rot] of list) {
    A.obox([x, y + 0.14, z], [0.45, 0.28, 0.72], [0, rot || 0, 0], "sacking");
    A.obox([x, y + 0.3, z], [0.36, 0.06, 0.6], [0, rot || 0, 0], "sacking");
  }
  A.endFurniture();
}
// bunk bed, tiers 1-3, long axis along Z
export function bunkZ(A, name, x0, x1, z0, z1, y, tiers = 2, frame = "plank_z", mat = "canvas") {
  A.beginFurniture(name);
  for (const x of [x0, x1 - 0.06]) for (const z of [z0, z1 - 0.06]) A.box([x, y, z], [x + 0.06, y + 0.45 + tiers * 0.65, z + 0.06], frame === "plank_z" ? "plank_y" : frame);
  for (let t = 0; t < tiers; t++) {
    const yy = y + 0.35 + t * 0.75;
    A.box([x0, yy, z0], [x0 + 0.05, yy + 0.12, z1], frame);
    A.box([x1 - 0.05, yy, z0], [x1, yy + 0.12, z1], frame);
    A.box([x0 + 0.05, yy + 0.03, z0 + 0.06], [x1 - 0.05, yy + 0.16, z1 - 0.06], mat);
    A.box([x0 + 0.1, yy + 0.16, z0 + 0.1], [x1 - 0.1, yy + 0.24, z0 + 0.45], "linen");
    A.box([x0 + 0.06, yy + 0.16, z0 + 0.6], [x1 - 0.06, yy + 0.2, z1 - 0.1], "wool_green");
  }
  A.endFurniture();
}
export function stove(A, name, x, z, y, flueTopY, flueTo = null) {
  A.beginFurniture(name);
  A.box([x - 0.22, y + 0.12, z - 0.22], [x + 0.22, y + 0.72, z + 0.22], "cast_iron");
  for (const sx of [-1, 1]) for (const sz of [-1, 1]) A.box([x + sx * 0.17 - 0.03, y, z + sz * 0.17 - 0.03], [x + sx * 0.17 + 0.03, y + 0.12, z + sz * 0.17 + 0.03], "cast_iron");
  A.box([x - 0.24, y + 0.72, z - 0.24], [x + 0.24, y + 0.76, z + 0.24], "cast_iron");
  A.box([x - 0.06, y + 0.76, z - 0.06], [x + 0.06, flueTopY, z + 0.06], "cast_iron");
  if (flueTo) A.box([Math.min(x, flueTo[0]), flueTopY - 0.12, Math.min(z, flueTo[1]) - 0.06], [Math.max(x, flueTo[0]), flueTopY, Math.max(z, flueTo[1]) + 0.06], "cast_iron");
  A.box([x + 0.22, y + 0.3, z - 0.12], [x + 0.235, y + 0.55, z + 0.12], "iron");
  A.endFurniture();
}
export function stool(A, name, x, z, y, h = 0.5) {
  A.beginFurniture(name);
  A.box([x - 0.17, y + h - 0.04, z - 0.17], [x + 0.17, y + h, z + 0.17], "plank_x");
  for (const sx of [-1, 1]) for (const sz of [-1, 1]) A.box([x + sx * 0.12 - 0.02, y, z + sz * 0.12 - 0.02], [x + sx * 0.12 + 0.02, y + h - 0.04, z + sz * 0.12 + 0.02], "plank_y");
  A.endFurniture();
}
export function lamp(A, x, y, z) { // ceiling bulkhead lamp (visual only)
  A.box([x - 0.09, y - 0.05, z - 0.09], [x + 0.09, y, z + 0.09], "steel_plate", { k: 0 });
  A.box([x - 0.06, y - 0.14, z - 0.06], [x + 0.06, y - 0.05, z + 0.06], "glass", { k: 0 });
}
