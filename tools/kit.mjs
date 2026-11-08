// Authoring kit for Normandy Mapmaker asset definitions.
// Produces the JSON format read by src/assets.cpp. All units are metres; +Y is up;
// a building's front faces local +Z. Boxes are { t:"b", c, s, r?, m, v, k, bv? }; B1 adds cylinders / cones / arches /
// wedges { t: "c" | "k" | "a" | "w" } (Asset.cyl / cone / arch / wedge).

const R3 = (v) => Math.round(v * 10000) / 10000;

export class Asset {
  constructor(meta) {
    this.meta = meta;
    this.materials = [];
    this.parts = [];
    this.furniture = [];
    this.interactables = [];
    this.panels = [];
    this._target = this.parts;
  }
  mat(name, albedo, roughness, pattern = "plain", extra = {}) {
    this.materials.push({ name, albedo, roughness, pattern, metallic: 0, grain: 1, ...extra });
  }
  // axis-aligned box from min/max corners
  box(min, max, m, o = {}) {
    const c = [0, 1, 2].map((i) => R3((min[i] + max[i]) / 2));
    const s = [0, 1, 2].map((i) => R3(Math.abs(max[i] - min[i])));
    if (s.some((x) => x < 1e-4)) return null;
    return this._put({ t: "b", c, s, m }, o);
  }
  // rotated box: centre, size, euler degrees
  obox(c, s, r, m, o = {}) {
    return this._put({ t: "b", c: c.map(R3), s: s.map(R3), r: r.map(R3), m }, o);
  }
  // shared options: v:0 hidden, k:0 no collider, bv (boxes) a mesh-only edge chamfer in metres (the collider stays the
  // box; never on destructible wall bays: the loader strips it there), r euler degrees, into a target list
  _put(p, o) {
    if (o.r && !p.r) p.r = o.r.map(R3);
    if (o.bv > 0 && p.t === "b") p.bv = R3(o.bv);
    if (o.v === 0) p.v = 0;
    if (o.k === 0) p.k = 0;
    (o.into || this._target).push(p);
    return p;
  }
  // B1 curved / sloped parts (src/assets.cpp appendCurved): each fills its box c (centre), s (size), rotated by o.r.
  // cylinder along local y; s[0], s[2] the diameters. o.n: 0 round (smooth), 4 square, 8 octagonal; o.seg segments
  cyl(c, s, m, o = {}) {
    const p = { t: "c", c: c.map(R3), s: s.map(R3), m };
    if (o.n) p.n = o.n;
    if (o.seg) p.seg = o.seg;
    if (o.top !== undefined && o.top !== 1) p.top = R3(o.top);
    return this._put(p, o);
  }
  // cone / spire / frustum along local y: o.top = top section / base (0 = a point), o.n as cyl
  cone(c, s, m, o = {}) {
    const p = { t: "k", c: c.map(R3), s: s.map(R3), m };
    if (o.n) p.n = o.n;
    if (o.seg) p.seg = o.seg;
    if (o.top) p.top = R3(o.top);
    return this._put(p, o);
  }
  // arch standing on the box's bottom face (springing), extruded along local z: s = [span, rise, depth].
  // o.th ring thickness (default 0.25); o.fill: the spandrel (the box minus the intrados, crown thickness th)
  arch(c, s, m, o = {}) {
    const p = { t: "a", c: c.map(R3), s: s.map(R3), m };
    if (o.th !== undefined) p.th = R3(o.th);
    if (o.fill) p.fill = 1;
    if (o.seg) p.seg = o.seg;
    return this._put(p, o);
  }
  // wedge: full height at local -z falling to o.top * height at +z (0 = a sharp edge): weatherings, sills, brackets
  wedge(c, s, m, o = {}) {
    const p = { t: "w", c: c.map(R3), s: s.map(R3), m };
    if (o.top) p.top = R3(o.top);
    return this._put(p, o);
  }
  // rounded box from min/max corners: every edge rounded by `rad` (render and collision; src/assets.cpp appendRound).
  // Not a damage-field bay: use it for plinths, roof slabs, hoods, piers, not for the destructible wall body.
  rbox(min, max, rad, m, o = {}) {
    const c = [0, 1, 2].map((i) => R3((min[i] + max[i]) / 2));
    const s = [0, 1, 2].map((i) => R3(Math.abs(max[i] - min[i])));
    if (s.some((x) => x < 1e-4)) return null;
    return this._put({ t: "r", c, s, rad: R3(Math.min(rad, ...s.map((x) => x / 2))), m }, o);
  }
  // triangular prism: triangle in (u,v) extruded along axis (0:x → (z,y), 1:y → (x,z), 2:z → (x,y))
  prism(axis, a0, a1, tri, m, o = {}) {
    const p = { t: "p", axis, a: [R3(a0), R3(a1)], tri: tri.map((q) => q.map(R3)), m };
    if (o.v === 0) p.v = 0;
    if (o.k === 0) p.k = 0;
    (o.into || this._target).push(p);
    return p;
  }
  // start a furniture group: visible parts don't collide, one invisible bounding collider is added
  beginFurniture(name) { this._furn = { name, parts: [] }; this._target = this._furn.parts; }
  endFurniture(collide = true) {
    const f = this._furn;
    if (collide && f.parts.length) {
      let mn = [1e9, 1e9, 1e9], mx = [-1e9, -1e9, -1e9];
      for (const p of f.parts) {
        if (p.t !== "b") continue;
        let he = p.s.map((x) => x / 2);
        if (p.r) { const e = Math.max(...he); he = [e, e, e]; }
        for (let i = 0; i < 3; i++) { mn[i] = Math.min(mn[i], p.c[i] - he[i]); mx[i] = Math.max(mx[i], p.c[i] + he[i]); }
      }
      for (const p of f.parts) p.k = 0;
      this.box(mn, mx, f.parts[0].m, { v: 0 });
    } else for (const p of f.parts) if (p.v !== 0) p.k = 0;   // explicit invisible colliders survive
    this.furniture.push(f);
    this._target = this.parts;
    this._furn = null;
  }
  json() {
    return { ...this.meta, materials: this.materials, parts: this.parts, furniture: this.furniture, interactables: this.interactables, panels: this.panels, walkthrough: this.walkthrough };
  }
}

// Point/box helpers for walls. axis "x": wall runs along X at constant Z (depth = z).
// axis "z": wall runs along Z at constant X (depth = x).
export function P(axis, a, y, d) { return axis === "x" ? [a, y, d] : [d, y, a]; }
export function wbox(A, axis, a0, a1, y0, y1, d0, d1, m, o = {}) {
  const p0 = P(axis, Math.min(a0, a1), Math.min(y0, y1), Math.min(d0, d1));
  const p1 = P(axis, Math.max(a0, a1), Math.max(y0, y1), Math.max(d0, d1));
  return A.box(p0, p1, m, o);
}

// subtract hole rectangles (a0,a1,y0,y1) from a wall rectangle
export function cutRect(a0, a1, y0, y1, holes) {
  const xs = new Set([a0, a1]);
  for (const h of holes) { if (h.a0 > a0 && h.a0 < a1) xs.add(h.a0); if (h.a1 > a0 && h.a1 < a1) xs.add(h.a1); }
  const xv = [...xs].sort((p, q) => p - q);
  const out = [];
  for (let i = 0; i + 1 < xv.length; i++) {
    const xa = xv[i], xb = xv[i + 1], xm = (xa + xb) / 2;
    const act = holes.filter((h) => h.a0 <= xm && h.a1 >= xm).map((h) => [Math.max(h.y0, y0), Math.min(h.y1, y1)]).filter((r) => r[1] > r[0]).sort((p, q) => p[0] - q[0]);
    let y = y0;
    for (const [hy0, hy1] of act) { if (hy0 > y + 1e-6) out.push([xa, xb, y, hy0]); y = Math.max(y, hy1); }
    if (y < y1 - 1e-6) out.push([xa, xb, y, y1]);
  }
  return out;
}

// Rotate p around a vertical hinge (glm::angleAxis about +Y convention).
export function rotY(p, pivot, deg) {
  const t = (deg * Math.PI) / 180, c = Math.cos(t), s = Math.sin(t);
  const x = p[0] - pivot[0], z = p[2] - pivot[2];
  return [pivot[0] + x * c + z * s, p[1], pivot[2] - x * s + z * c];
}
// choose rotation sign so a leaf's midpoint moves along the wanted world direction (+1/-1 on the wall normal axis)
export function swingSign(axis, mid, hinge, want) {
  const p = rotY(mid, hinge, 90);
  const n = axis === "x" ? 2 : 0;
  return (p[n] - hinge[n]) * want > 0 ? 1 : -1;
}
