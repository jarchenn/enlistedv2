"""Read-only view of the asset catalogue (content/assets/*.json): the fields map authoring needs."""
import json
import os


class Asset:
    def __init__(self, j, path):
        self.raw = j
        self.path = path
        self.id = j["id"]
        self.version = j.get("version", 1)
        self.name = j.get("name", self.id)
        self.category = j.get("category", "")
        fp = j.get("footprint", [1, 1])
        self.fw, self.fd = float(fp[0]), float(fp[1])
        self.defense = (j.get("defense") or {}).get("kind", "")
        self.blocks_vehicles = bool((j.get("defense") or {}).get("blocksVehicles", False))
        self.path_on = "path" in j and isinstance(j["path"], dict)
        self.tree = "tree" in j
        self.rack = "rack" in j
        self.nav = j.get("nav") or {}
        self.cuts = j.get("terrainCut") or []
        self.companions = j.get("companions") or []
        self.station = j.get("station")
        self.enterable = j.get("enterable", False)

    @property
    def half(self):
        return (self.fw * 0.5, self.fd * 0.5)

    @property
    def gives_cover(self):
        n = self.nav
        return bool(n.get("cover")) or n.get("effect") in ("block",) or self.category in ("Bunkers", "Houses", "Large buildings", "Small buildings", "Landmarks")

    @property
    def firing_points(self):
        return self.nav.get("firingPoints") or []


class Catalog:
    def __init__(self, content_dir):
        self.content_dir = content_dir
        self.assets = {}
        d = os.path.join(content_dir, "assets")
        for f in sorted(os.listdir(d)):
            if not f.endswith(".json"):
                continue
            p = os.path.join(d, f)
            try:
                with open(p, encoding="utf-8") as fh:
                    j = json.load(fh)
            except (OSError, ValueError):
                continue
            if "id" in j:
                self.assets[j["id"]] = Asset(j, p)

    def __getitem__(self, k):
        if k not in self.assets:
            raise KeyError("asset '%s' is not in the catalogue" % k)
        return self.assets[k]

    def __contains__(self, k):
        return k in self.assets

    def ids(self):
        return sorted(self.assets)
