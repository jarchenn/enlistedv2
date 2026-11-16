"""MapBuilder: places catalogue instances on the sculpted grid the way the editor would.

* Heights (MAP1, placement_y / fill / reseat_all): trees sit on the lowest ground under the trunk, small pieces and
  defences on the lowest footprint sample, large pieces on the highest with the ground raised under any edge that would
  float (raise-only blend ring); every auto-seated piece is re-seated on the final ground. Paths take the centre height.
* Footprints are oriented boxes in a spatial hash; a placement that overlaps another is refused unless the classes
  are allowed to overlap (`ok_with`) or both pieces belong to the same chain group.
* Companions (asset "companions", e.g. the R667's PaK 40) are added with their host, as the palette does.
* Terrain cuts (asset "terrainCut") are tracked so later heights see the trench / ditch floors.
"""
import math

from .geom import (local_to_world, asset_yaw_along, asset_yaw_facing, wrap_deg, obb_overlap, point_at,
                   polyline_length, resample, in_poly, poly_bounds, polyline_dist, dist, norm, seg_dist)


class Frame:
    """Attack-axis frame: s = metres from the attacker's map edge (z = -depth/2) toward the far edge (+Z)."""

    def __init__(self, width, depth):
        self.width, self.depth = float(width), float(depth)

    def w(self, x, s):
        return (float(x), float(s) - self.depth * 0.5)

    def s_of(self, z):
        return z + self.depth * 0.5

    def pts(self, xs_pairs):
        return [self.w(x, s) for x, s in xs_pairs]


SOLID, VEG, FLAT, WIRE, NONE = "solid", "veg", "flat", "wire", "none"


def default_class(a):
    if a.path_on:
        return FLAT
    if a.category == "Vegetation" and a.id.startswith(("tree_", "shrub_")):
        return VEG
    if a.id.startswith("wire_") or a.id in ("bch_minefield_fence", "bch_minen_sign", "fence_post_wire"):
        return WIRE
    return SOLID


class MapBuilder:
    CELL = 16.0

    def __init__(self, catalog, grid, frame, seed=1, water_level=-1000.0):
        self.cat, self.grid, self.frame = catalog, grid, frame
        self.water = water_level
        self.inst = []            # dicts: id, asset, pos [x, y, z], yaw, extra, tags, cls, group
        self.by_id = {}
        self.next_id = 1
        self.hash = {}
        self.cuts = []            # (poly, floorY, topY, bank, bounds, owner)
        self.routes = []
        self.keepouts = []        # (kind, data, half) polyline keep-outs for scatter: ("line", pts, half) / ("poly", poly, 0)
        self.cover_points = []    # extra non-instance cover (terrain craters): (x, z, label)
        self.rejects = {}
        self.seed = seed
        grid.builder = self   # MAP1: Grid.write_png re-seats every placement on the finished ground first
        self._group = 0

    # ---------------------------------------------------------------- ground
    def ground(self, x, z, ignore_owner=None, before_fills=False):
        base = getattr(self, "prefill", None) if before_fills else None   # MAP1: the ground before any fill()
        h = self.grid.sample(x, z, base)
        for poly, floor, top, bank, b, owner in self.cuts:
            if owner == ignore_owner or x < b[0] or z < b[1] or x > b[2] or z > b[3] or not in_poly((x, z), poly):
                continue
            d = min(seg_dist((x, z), poly[i], poly[(i + 1) % len(poly)])[0] for i in range(len(poly)))
            k = min(1.0, d / bank) if bank > 1e-3 else 1.0
            h = min(h, top - (top - floor) * k)
        return h

    NINE = ((0, 0), (-1, -1), (1, -1), (-1, 1), (1, 1), (0, -1), (0, 1), (-1, 0), (1, 0))
    def padded(self, a):
        """The engine levels a pad under these itself (terrain_blend.cpp blendPinned / snap.cpp crushBuilding):
        enterable with wall panels and no terrain cut, or Atlantic Wall pieces of 6 m2 and more."""
        if a.cuts or a.path_on:
            return False
        return (a.enterable and bool(a.raw.get("panels"))) or (a.category == "Atlantic Wall" and a.fw * a.fd >= 6.0)

    def _samples(self, a, x, z, yaw, inset=0.1, before_fills=False):
        hx, hz = max(0.02, a.half[0] - inset), max(0.02, a.half[1] - inset)
        return [(lambda w: (w[0], w[1], self.ground(w[0], w[1], before_fills=before_fills)))(local_to_world((x, z), yaw, sx * hx, sz * hz)) for sx, sz in self.NINE]

    def placement_y(self, a, x, z, yaw):
        """MAP1: the seat height. Paths: the centre. Trees: the lowest ground under the trunk, 5 cm in. Scenery and small
        pieces (< 6 m2, defences): the lowest footprint sample (never floats; a placement whose centre would sit > 15 cm
        under is refused as too steep, try_place moves it; the grid's 3.1 m cells make ground fills for small pieces
        lift their neighbours). Large pieces: the highest sample of the ground before any fill (the editor's rule; a
        fill never lifts the next seat, so chains cannot creep); fill() then raises the ground under any low edge."""
        if a.path_on:
            return self.ground(x, z)
        if a.tree:
            r = 0.3
            return min(self.ground(x + dx, z + dz) for dx, dz in ((0, 0), (r, 0), (-r, 0), (0, r), (0, -r))) - 0.05
        if self.small(a) or a.category == "Scenery":   # the lowest corner of the ground as it is (Scenery: the base band sinks)
            return min(s[2] for s in self._samples(a, x, z, yaw))
        hs = [s[2] for s in self._samples(a, x, z, yaw, before_fills=True)]   # large: never the filled ground (no creep)
        lo, hi = min(hs), max(hs)
        # the editor's highest sample on gentle ground; on a slope 30 cm over the lowest corner: the uphill part beds
        # into the ground and fill() raises the downhill edge by 30 cm at most (raising it to the top corner piled
        # dunes up to 4 m high under wrecks and rubble on the beach)
        return hi if hi - lo <= 0.3 else lo + 0.3

    def near_cut(self, a, x, z, yaw, margin=1.5):
        """MAP1: a small piece within 1.5 m of a trench / ditch cut (the engine's trench lines cut wider than the
        pieces: a crate on the generator's lip hung over the engine's trench)."""
        r = math.hypot(*a.half) + margin
        for poly, floor, top, bank, b, owner in self.cuts:
            if x < b[0] - r or z < b[1] - r or x > b[2] + r or z > b[3] + r:
                continue
            if in_poly((x, z), poly) or min(seg_dist((x, z), poly[i], poly[(i + 1) % len(poly)])[0] for i in range(len(poly))) < r:
                return True
        return False

    def small(self, a):
        return bool(a.defense) or a.fw * a.fd < 6.0

    def too_steep(self, a, x, z, yaw):
        """MAP1: a small piece whose centre stands > 15 cm over its lowest corner would sink visibly: another spot."""
        hs = [s[2] for s in self._samples(a, x, z, yaw)]
        return hs[0] - min(hs) > 0.15

    def fills(self, a):
        return not (self.small(a) or a.tree or a.path_on or self.padded(a) or a.cuts or a.category in ("Water", "Scenery")
                    or a.raw.get("nature") or a.raw.get("scatter") or a.raw.get("line"))

    def fill(self, a, x, z, yaw, y):
        """MAP1: raise the ground under a placed footprint wherever it lies more than 4 cm below the base (every vertex
        of the grid cells it touches, then a raise-only 3 m blend ring), so no edge of a large piece floats."""
        if all(s[2] >= y - 0.04 for s in self._samples(a, x, z, yaw, inset=0.0)):
            return False
        if getattr(self, "prefill", None) is None:
            self.prefill = list(self.grid.h)
        hx, hz = a.half[0] + 0.3, a.half[1] + 0.3
        poly = [local_to_world((x, z), yaw, sx * hx, sz * hz) for sx, sz in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
        ring = 3.0
        from .geom import seg_dist, in_poly as inp, smoothstep as ss, lerp as lp

        from .terrain import KCELL
        full = KCELL * 1.05   # every vertex of a grid cell the footprint touches reaches the base (cells are ~3.1 m)

        def fn(px, pz, h):
            d = 0.0 if inp((px, pz), poly) else min(seg_dist((px, pz), poly[i], poly[(i + 1) % 4])[0] for i in range(4))
            if d <= full:
                return max(h, y - 0.02)
            if d >= full + ring:
                return h
            return max(h, lp(y - 0.02, h, ss(0, ring, d - full)))
        xs = [p[0] for p in poly]; zs = [p[1] for p in poly]
        m = full + ring
        self.grid.apply(fn, (min(xs) - m, min(zs) - m, max(xs) + m, max(zs) + m))
        self.filled = getattr(self, "filled", 0) + 1
        return True

    def grade(self, a, x, z, yaw):
        hx, hz = a.half
        hs = []
        for lx, lz in ((0, 0), (-hx, -hz), (hx, -hz), (-hx, hz), (hx, hz)):
            wx, wz = local_to_world((x, z), yaw, lx, lz)
            hs.append(self.ground(wx, wz))
        return max(hs) - min(hs), min(hs)

    # ---------------------------------------------------------------- occupancy
    def _cells(self, cx, cz, r):
        c = self.CELL
        for gx in range(int(math.floor((cx - r) / c)), int(math.floor((cx + r) / c)) + 1):
            for gz in range(int(math.floor((cz - r) / c)), int(math.floor((cz + r) / c)) + 1):
                yield (gx, gz)

    def overlaps(self, a, x, z, yaw, ok_with=(), group=None, shrink=0.05):
        hx, hz = a.half[0] - shrink, a.half[1] - shrink
        r = math.hypot(hx, hz)
        seen = set()
        for key in self._cells(x, z, r):
            for e in self.hash.get(key, ()):
                if e["id"] in seen:
                    continue
                seen.add(e["id"])
                if e["cls"] in ok_with or (group is not None and e.get("group") == group):
                    continue
                o = self.cat[e["asset"]]
                if math.hypot(e["pos"][0] - x, e["pos"][2] - z) > r + math.hypot(*o.half):
                    continue
                if obb_overlap((x, z), (hx, hz), yaw, (e["pos"][0], e["pos"][2]), (o.half[0] - shrink, o.half[1] - shrink), e["yaw"]):
                    return e
        return None

    def near(self, x, z, r, classes=None):
        out, seen = [], set()
        for key in self._cells(x, z, r):
            for e in self.hash.get(key, ()):
                if e["id"] in seen:
                    continue
                seen.add(e["id"])
                if classes and e["cls"] not in classes:
                    continue
                if math.hypot(e["pos"][0] - x, e["pos"][2] - z) <= r:
                    out.append(e)
        return out

    def _index(self, e):
        a = self.cat[e["asset"]]
        r = math.hypot(*a.half)
        for key in self._cells(e["pos"][0], e["pos"][2], r):
            self.hash.setdefault(key, []).append(e)

    def new_group(self):
        self._group += 1
        return self._group

    # ---------------------------------------------------------------- placement
    def place(self, asset_id, x, z, yaw=0.0, *, check=True, ok_with=None, cls=None, group=None, tags=(), extra=None,
              y=None, dy=0.0, water_ok=False, max_grade=None, companions=True):
        """Place one instance; returns its dict or None (reason counted in self.rejects)."""
        a = self.cat[asset_id]
        yaw = wrap_deg(yaw)
        cls = cls or default_class(a)
        if ok_with is None:
            ok_with = {FLAT, NONE} if cls != VEG else {FLAT, NONE, WIRE}
            if a.defense == "sandbag":
                ok_with = set(ok_with)
        if check:
            if not self.grid.in_build(x, z):
                return self._reject(asset_id, "outside")
            if self.overlaps(a, x, z, yaw, ok_with, group):
                return self._reject(asset_id, "overlap")
            if max_grade is not None:
                g, _ = self.grade(a, x, z, yaw)
                if g > max_grade:
                    return self._reject(asset_id, "grade")
            if not water_ok and self.ground(x, z) < self.water + 0.2:
                return self._reject(asset_id, "water")
            if y is None and dy == 0 and not a.tree and not a.path_on and not a.rack and self.small(a) and self.too_steep(a, x, z, yaw):
                return self._reject(asset_id, "steep")
            if y is None and not a.tree and not a.path_on and not a.rack and self.small(a) and self.near_cut(a, x, z, yaw):   # spawn racks are never refused
                return self._reject(asset_id, "trench")
        py = (self.placement_y(a, x, z, yaw) if y is None else y) + dy
        auto = y is None and dy == 0 and not a.cuts
        if auto and cls != NONE and self.fills(a):
            self.fill(a, x, z, yaw, py)
        if self.padded(a) and self.grid.in_build(x, z):
            self.pad(a, x, z, yaw, py)
        e = {"id": self.next_id, "asset": asset_id, "pos": [x, py, z], "yaw": yaw, "extra": dict(extra or {}),
             "tags": set(tags), "cls": cls, "group": group, "auto": auto}
        if a.blocks_vehicles:
            e["extra"]["blocksVehicles"] = True
        self.next_id += 1
        self.inst.append(e)
        self.by_id[e["id"]] = e
        if cls != NONE:
            self._index(e)
        for c in a.cuts:
            poly = [local_to_world((x, z), yaw, p[0], p[1]) for p in c["outline"]]
            self.cuts.append((poly, py - c.get("depth", 1.0), py, c.get("bank", 0.3), poly_bounds(poly), e["id"]))
        if companions:
            for c in a.companions:
                if c.get("asset") not in self.cat:
                    continue
                p = c["pos"]
                wx, wz = local_to_world((x, z), yaw, p[0], p[2])
                ce = self.place(c["asset"], wx, wz, yaw + c.get("yaw", 0.0), check=False, cls=NONE, y=py + p[1],
                                tags=set(tags) | {"companion"}, companions=False)
                ce["host"] = e["id"]
        return e

    HOSTS = ("Atlantic Wall", "Bunkers")

    def _hosts(self):
        return [h for h in self.inst if self.padded(self.cat[h["asset"]]) or (self.cat[h["asset"]].category in self.HOSTS
                and self.cat[h["asset"]].fw * self.cat[h["asset"]].fd >= 6.0)]

    def _inside(self, h, x, z, grow=0.0):
        a = self.cat[h["asset"]]
        r = math.radians(-h["yaw"]); c, s_ = math.cos(r), math.sin(r)
        dx, dz = x - h["pos"][0], z - h["pos"][2]
        lx, lz = dx * c + dz * s_, -dx * s_ + dz * c
        return abs(lx) <= a.half[0] + grow and abs(lz) <= a.half[1] + grow

    def tidy(self):
        """MAP1, before the re-seat: drop vegetation within 3 m of a building / bunker / Atlantic Wall footprint (the
        engine levels its own pad and ring there, burying it), and mark small pieces given an explicit height for
        re-seating unless they stand inside such a footprint (on a roof, a platform, a floor)."""
        hosts = self._hosts()
        drop = [e for e in self.inst if e.get("auto") and "host" not in e and (self.cat[e["asset"]].tree or self.cat[e["asset"]].raw.get("nature"))
                and any(self._inside(h, e["pos"][0], e["pos"][2], 3.0) for h in hosts if h is not e)]
        for e in drop:
            self.remove(e)
        n = 0
        for e in self.inst:   # companions standing outside their host (a church's churchyard) seat on their own ground
            h = self.by_id.get(e.get("host"))
            if h is not None and not self._inside(h, e["pos"][0], e["pos"][2], 0.2) and not self.cat[e["asset"]].cuts:
                e["auto"] = True; e["free"] = True; n += 1
        for e in self.inst:
            a = self.cat[e["asset"]]
            if e.get("auto") or "host" in e or e["cls"] == NONE or a.cuts or a.tree or a.path_on or not self.small(a) or a.category == "Water":
                continue
            if any(self._inside(h, e["pos"][0], e["pos"][2], 0.2) for h in hosts if h is not e):
                continue
            e["auto"] = True; n += 1
        self.tidied = (len(drop), n)
        return self.tidied

    def pad(self, a, x, z, yaw, y):
        """MAP1: level the ground the way the engine will (terrain_blend.cpp PAD: the walls' core, or the footprint,
        grown one step, at the instance's height, then a 2-3.5 m ring), so the dressing around buildings, casemates and
        the stronghold is seated on the ground the engine draws rather than on the ground before its pad."""
        ew = a.raw.get("earthwork") if isinstance(a.raw.get("earthwork"), dict) else None
        if ew and ew.get("core"):
            c = ew["core"]; hx, hz = float(c[0]), float(c[1]); ox, oz = (float(c[2]), float(c[3])) if len(c) >= 4 else (0.0, 0.0)
        else:
            hx, hz, ox, oz = a.half[0], a.half[1], 0.0, 0.0
        cx, cz = local_to_world((x, z), yaw, ox, oz)
        hs = [self.grid.sample(*local_to_world((cx, cz), yaw, sx * hx, sz * hz)) for sx, sz in self.NINE]
        dmax = max(abs(h - y) for h in hs)
        if dmax < 0.05 and not ew:
            return False
        from .terrain import KCELL
        from .geom import seg_dist, in_poly as inp, smoothstep as ss, lerp as lp
        grow = 0.0 if ew else KCELL
        ring = min(3.5, max(2.0, 2.0 + 1.5 * dmax)) + KCELL * 0.5
        poly = [local_to_world((cx, cz), yaw, sx * (hx + grow), sz * (hz + grow)) for sx, sz in ((-1, -1), (1, -1), (1, 1), (-1, 1))]

        def fn(px, pz, h):
            d = 0.0 if inp((px, pz), poly) else min(seg_dist((px, pz), poly[i], poly[(i + 1) % 4])[0] for i in range(4))
            if d >= ring:
                return h
            return lp(y, h, ss(0, ring, d))
        xs = [p[0] for p in poly]; zs = [p[1] for p in poly]
        bounds = (min(xs) - ring, min(zs) - ring, max(xs) + ring, max(zs) + ring)
        self.grid.apply(fn, bounds)
        if getattr(self, "prefill", None) is not None:   # a pad is real ground, not a fill: the large seats see it too
            cur = self.grid.h; self.grid.h = self.prefill
            self.grid.apply(fn, bounds)
            self.grid.h = cur
        self.padded_n = getattr(self, "padded_n", 0) + 1
        return True

    def reseat_all(self, passes=8):
        """MAP1: re-seat every auto-seated instance on the finished ground (later fills raise ground under earlier
        pieces) and fill again where an edge would float; companions move with their host. Called by Grid.write_png."""
        self.tidy()
        moved = 0
        for _ in range(passes):   # until nothing moves: a fill raises ground under neighbours seated before it
            moved_now = 0
            for e in self.inst:
                if not e.get("auto") or ("host" in e and not e.get("free")):
                    continue
                a = self.cat[e["asset"]]
                x, z = e["pos"][0], e["pos"][2]
                py = self.placement_y(a, x, z, e["yaw"])
                if e["cls"] != NONE and self.fills(a):
                    self.fill(a, x, z, e["yaw"], py)
                d = py - e["pos"][1]
                if abs(d) > 0.005:
                    moved += 1
                    moved_now += 1
                    e["pos"][1] = py
                    for c in self.inst:
                        if c.get("host") == e["id"] and not c.get("free"):
                            c["pos"][1] += d
            if moved_now == 0:
                break
        self.reseat_passes = _ + 1
        self.reseated = moved
        bad = [e for e in self.inst if e.get("auto") and "host" not in e and self.ground(e["pos"][0], e["pos"][2]) - e["pos"][1] > 0.1
               and not self.cat[e["asset"]].tree and self.cat[e["asset"]].category != "Scenery"]
        flo = [e for e in self.inst if e.get("auto") and "host" not in e and not self.cat[e["asset"]].tree and self.cat[e["asset"]].category != "Scenery"
               and not self.cat[e["asset"]].path_on and not self.padded(self.cat[e["asset"]]) and not self.cat[e["asset"]].raw.get("nature")
               and not self.cat[e["asset"]].raw.get("scatter") and min(s[2] for s in self._samples(self.cat[e["asset"]], e["pos"][0], e["pos"][2], e["yaw"])) < e["pos"][1] - 0.05]
        print("MAP1 tidy: %d plants dropped near structures, %d explicit-height pieces re-seated; %d auto pieces float on the grid%s" % (self.tidied + (len(flo),)
              + ("".join(" | %s %.2f" % (e["asset"], e["pos"][1] - min(s[2] for s in self._samples(self.cat[e["asset"]], e["pos"][0], e["pos"][2], e["yaw"]))) for e in flo[:6]),)))
        print("MAP1 reseat: %d moves in %d passes, %d auto pieces still > 10 cm under the ground at the centre%s" % (moved, self.reseat_passes, len(bad),
              "".join(" | %s %.2f" % (e["asset"], self.ground(e["pos"][0], e["pos"][2]) - e["pos"][1]) for e in bad[:6])))
        return moved

    def _reject(self, asset_id, why):
        self.rejects[(asset_id, why)] = self.rejects.get((asset_id, why), 0) + 1
        return None

    def remove(self, e):
        self.inst.remove(e)
        del self.by_id[e["id"]]
        for lst in self.hash.values():
            if e in lst:
                lst.remove(e)
        self.cuts = [c for c in self.cuts if c[5] != e["id"]]

    def try_place(self, asset_id, x, z, yaw, rng, tries=12, radius=4.0, **kw):
        """place(), nudging the position up to `tries` times within `radius` when refused."""
        e = self.place(asset_id, x, z, yaw, **kw)
        k = 0
        while e is None and k < tries:
            k += 1
            ang = rng.uniform(0, 2 * math.pi)
            r = radius * math.sqrt(rng.random())
            e = self.place(asset_id, x + r * math.cos(ang), z + r * math.sin(ang), yaw, **kw)
        return e

    # ---------------------------------------------------------------- linear features
    def chain(self, assets, pts, *, along="x", front=None, gaps=(), start=0.0, end=None, spacing=0.0, jitter_yaw=0.0,
              rng=None, group=None, tags=(), check=True, ok_with=None, pattern_rng=False, max_grade=None, **kw):
        """Lay joining pieces end to end along a polyline.
        assets: an asset id, or a list cycled in order (or picked at random with pattern_rng and rng).
        along: the asset axis the pieces join along ("x" hedgerows, trenches, wire fences; "z" sunken lanes).
        front: world point the asset's +Z should face (trenches: the enemy); None keeps the travel-left side.
        gaps: (s0, s1) arc-length intervals left open. Returns the placed instances."""
        if isinstance(assets, str):
            assets = [assets]
        group = group if group is not None else self.new_group()
        L = polyline_length(pts)
        end = L if end is None else min(L, end)
        s = start
        out, k = [], 0
        while s < end - 0.5:
            aid = rng.choice(assets) if pattern_rng and rng else assets[k % len(assets)]
            a = self.cat[aid]
            plen = a.fw if along == "x" else a.fd
            if s + plen > end + 0.25 * plen:
                break
            mid = s + plen * 0.5
            if any(g0 - plen * 0.5 < mid < g1 + plen * 0.5 for g0, g1 in gaps):
                s += plen + spacing
                k += 1
                continue
            (px, pz), t = point_at(pts, mid)
            yaw = asset_yaw_along(t[0], t[1]) if along == "x" else asset_yaw_facing(t[0], t[1])
            if front is not None:
                c, sn = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
                fz = (sn, c)
                if fz[0] * (front[0] - px) + fz[1] * (front[1] - pz) < 0:
                    yaw += 180.0
            if jitter_yaw and rng:
                yaw += rng.uniform(-jitter_yaw, jitter_yaw)
            e = self.place(aid, px, pz, yaw, check=check, ok_with=ok_with, group=group, tags=tags, max_grade=max_grade, **kw)
            if e:
                out.append(e)
            s += plen + spacing
            k += 1
        return out

    def belt(self, assets, pts, spacing, rng, *, jitter_along=0.3, jitter_across=0.0, gaps=(), yaw=None,
             yaw_jitter=0.0, rows=1, row_gap=0.0, row_stagger=0.5, tags=(), **kw):
        """Obstacle belt: one or more rows of single obstacles along a polyline, spacing in metres, irregular
        (jittered along / across, per-obstacle yaw jitter) with open gaps (arc-length intervals)."""
        out = []
        L = polyline_length(pts)
        for r in range(rows):
            off = (r - (rows - 1) * 0.5) * row_gap
            s = spacing * (row_stagger * r % 1.0)
            while s <= L:
                if not any(g0 <= s <= g1 for g0, g1 in gaps):
                    (px, pz), t = point_at(pts, s + rng.uniform(-jitter_along, jitter_along) * spacing)
                    nx, nz = -t[1], t[0]
                    a = off + rng.uniform(-jitter_across, jitter_across)
                    aid = rng.choice(assets) if isinstance(assets, (list, tuple)) else assets
                    yw = yaw if yaw is not None else asset_yaw_along(t[0], t[1])
                    e = self.place(aid, px + nx * a, pz + nz * a, yw + rng.uniform(-yaw_jitter, yaw_jitter), tags=tags, **kw)
                    if e:
                        out.append(e)
                s += spacing
        return out

    def scatter(self, assets, region, density, rng, *, min_dist=None, bounds=None, slope_max=None, yaw=None,
                avoid_routes=True, tags=(), max_count=None, accept=None, **kw):
        """Jittered density scatter (dart throwing, never a grid): about density * area instances per m2 inside
        region (a polygon or a predicate f(x, z) -> bool), at least min_dist apart (default 0.55 / sqrt(density)).
        assets: id or [(id, weight), ...]. accept(x, z) -> probability 0..1 thins the field (density gradients)."""
        if isinstance(region, (list, tuple)):
            poly = region
            bounds = bounds or poly_bounds(poly)
            inside = lambda x, z: in_poly((x, z), poly)
        else:
            inside = region
            assert bounds, "scatter with a predicate needs bounds"
        x0, z0, x1, z1 = bounds
        # estimate the region area on a 4 m lattice
        n_in, n_all = 0, 0
        step = 4.0
        zz = z0
        while zz < z1:
            xx = x0
            while xx < x1:
                n_all += 1
                if inside(xx, zz):
                    n_in += 1
                xx += step
            zz += step
        area = n_in * step * step
        target = int(area * density) if max_count is None else min(max_count, int(area * density))
        md = min_dist if min_dist is not None else 0.55 / math.sqrt(density)
        pool = assets if isinstance(assets, (list, tuple)) else [(assets, 1.0)]
        tot = sum(w for _, w in pool)
        local = {}
        cs = max(md, 1.0)
        out = []
        tries = 0
        while len(out) < target and tries < target * 25 + 50:
            tries += 1
            x, z = rng.uniform(x0, x1), rng.uniform(z0, z1)
            if not inside(x, z):
                continue
            if accept and rng.random() > accept(x, z):
                continue
            gx, gz = int(x // cs), int(z // cs)
            if any(math.hypot(px - x, pz - z) < md for dx in (-1, 0, 1) for dz in (-1, 0, 1) for px, pz in local.get((gx + dx, gz + dz), ())):
                continue
            if slope_max is not None and self.grid.slope_deg(x, z) > slope_max:
                continue
            if avoid_routes and self.in_keepout(x, z):
                continue
            r = rng.uniform(0, tot)
            for aid, w in pool:
                r -= w
                if r <= 0:
                    break
            yw = rng.uniform(-180, 180) if yaw is None else yaw
            e = self.place(aid, x, z, yw, tags=tags, **kw)
            if e:
                out.append(e)
                local.setdefault((gx, gz), []).append((x, z))
        return out

    # ---------------------------------------------------------------- routes (surface paint) and keep-outs
    def route(self, kind, pts, width, wet=None, strength=None, keepout=True, tags=()):
        r = {"kind": kind, "width": width, "pts": [[round(p[0], 2), round(p[1], 2)] for p in pts]}
        if wet is not None:
            r["wet"] = wet
        if strength is not None:
            r["strength"] = strength
        self.routes.append(r)
        if keepout:
            self.keepouts.append(("line", pts, width * 0.5 + 1.0))
        return r

    def keepout_poly(self, poly):
        self.keepouts.append(("poly", poly, 0.0))

    def keepout_line(self, pts, half):
        self.keepouts.append(("line", pts, half))

    def in_keepout(self, x, z):
        for kind, data, half in self.keepouts:
            if kind == "poly":
                b = poly_bounds(data)
                if b[0] <= x <= b[2] and b[1] <= z <= b[3] and in_poly((x, z), data):
                    return True
            else:
                b = poly_bounds(data)
                if b[0] - half <= x <= b[2] + half and b[1] - half <= z <= b[3] + half and polyline_dist((x, z), data)[0] < half:
                    return True
        return False

    # ---------------------------------------------------------------- queries
    def tagged(self, tag):
        return [e for e in self.inst if tag in e["tags"]]

    def count(self, asset_id=None):
        return sum(1 for e in self.inst if asset_id is None or e["asset"] == asset_id)

    def cover_gaps(self, path, lateral=15.0, step=5.0, min_height_assets=None):
        """Walk a polyline and return the stretches (s0, s1, length) with no cover within `lateral` m: instances whose
        asset gives cover (nav cover / blocking / buildings) or registered cover_points (craters, banks)."""
        pts = resample(path, step)
        gaps, last, s = [], 0.0, 0.0
        prev = pts[0]
        for p in pts:
            s += dist(prev, p)
            prev = p
            covered = any(self.cat[e["asset"]].gives_cover or "cover" in e["tags"]
                          for e in self.near(p[0], p[1], lateral, classes={SOLID, VEG, NONE}))
            covered = covered or any(math.hypot(cx - p[0], cz - p[1]) <= lateral for cx, cz, _ in self.cover_points)
            if covered:
                if s - last > 0:
                    gaps.append((last, s, s - last))
                last = s
        if s - last > 0:
            gaps.append((last, s, s - last))
        return gaps
