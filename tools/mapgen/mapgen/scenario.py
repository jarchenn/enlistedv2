"""Scenario v2 (src/scenario.h, Scenario::toJson) builder: objectives, spawns with weapons racks, defense slots,
and the SPEC 1.3 / 2.1 checks (validateScenario plus the default-map distance bands)."""
import math

from .geom import dist, look_yaw, local_to_world, wrap_deg

SPEC21 = {"objSpacing": (450.0, 600.0), "attackerBefore": (300.0, 450.0), "defenderBehind": (250.0, 500.0)}


class ScenarioBuilder:
    def __init__(self, builder, defending="Axis"):
        self.b = builder
        self.objectives = []
        self.spawns = []
        self.serial = 1
        self.rules = {"defendingFaction": defending, "captureModel": "timed", "captureSpeedCap": 3.0}

    def _id(self, prefix):
        s = "%s%d" % (prefix, self.serial)
        self.serial += 1
        return s

    def objective(self, name, x, z, *, radius=25.0, capture_time=45.0, attack_from=None, linked=(), slots=(),
                  routes=(), y_min=-2.0, y_max=4.0, polygon=None):
        """attack_from: (x, z) the attackers come from (sets the attack arrow); default the previous objective."""
        order = len(self.objectives)
        if attack_from is None:
            attack_from = (self.objectives[-1]["pos"][0], self.objectives[-1]["pos"][2]) if self.objectives else (x, z - 100)
        ay = look_yaw(x - attack_from[0], z - attack_from[1])
        o = {"id": self._id("obj"), "label": chr(ord("A") + order), "displayName": name, "order": order,
             "pos": [round(x, 3), round(self.b.ground(x, z), 3), round(z, 3)], "yaw": 0.0,
             "captureShape": {"type": "polygon", "points": [[round(p[0], 2), round(p[1], 2)] for p in polygon]} if polygon else {"type": "circle", "radius": radius},
             "radius": radius, "yMin": y_min, "yMax": y_max, "captureTime_s": capture_time, "attackYaw": round(ay, 2),
             "routes": [{"lane": ln, "points": [[round(p[0], 2), round(self.b.ground(p[0], p[1]), 2), round(p[1], 2)] for p in pts]} for ln, pts in routes],
             "linkedStructures": [int(i) for i in linked], "defenseSlots": list(slots)}
        self.objectives.append(o)
        return o

    def spawn(self, role, phases, x, z, *, label=None, face=None, rack="auto", rack_offset=None, faction=None):
        """Spawn marker (role "attacker" / "defender") for the given phase indices. rack: "auto" places the team rack
        2.5-4 m beside the marker (mpui placeRack: right / left of the facing), an asset id places that rack, None
        places none, or an existing instance dict links it."""
        phases = list(phases)
        tgt = self.objectives[phases[0]]["pos"] if self.objectives else [x, 0, z + 50]
        f = face if face is not None else (tgt[0], tgt[2])
        yaw = look_yaw(f[0] - x, f[1] - z)
        m = {"id": self._id("spawn"), "label": label or ("%s %s" % ("Attackers" if role == "attacker" else "Defenders", chr(ord("A") + phases[0]))),
             "role": role, "phases": phases, "pos": [round(x, 3), round(self.b.ground(x, z), 3), round(z, 3)], "yaw": round(yaw, 2), "rack": 0}
        fac = faction or (self.rules["defendingFaction"] if role == "defender" else ("Allies" if self.rules["defendingFaction"] == "Axis" else "Axis"))
        rid = None
        if isinstance(rack, dict):
            rid = rack["id"]
        elif rack:
            aid = rack if rack != "auto" else ("rack_allied" if fac == "Allies" else "rack_axis")
            fw = (-math.sin(math.radians(yaw)), -math.cos(math.radians(yaw)))
            rt = (-fw[1], fw[0])
            rt = (math.cos(math.radians(yaw)), -math.sin(math.radians(yaw)))   # attackRight(): the marker's right
            offs = [rack_offset] if rack_offset else [(2.5, 0.5), (-2.5, 0.5), (4.0, 0.5), (-4.0, 0.5), (2.5, -2.5), (-2.5, -2.5), (6.0, 1.0), (-6.0, 1.0)]
            for side, ahead in offs:
                px, pz = x + rt[0] * side + fw[0] * ahead, z + rt[1] * side + fw[1] * ahead
                e = self.b.place(aid, px, pz, yaw + 90.0, tags={"rack"})
                if e:
                    rid = e["id"]
                    break
        m["rack"] = int(rid or 0)
        self.spawns.append(m)
        return m

    def to_json(self, rules_extra=None):
        r = dict(self.rules)
        if rules_extra:
            r.update(rules_extra)
        return {"version": 2, "objectives": self.objectives, "spawns": self.spawns, "rules": r, "devTestMap": False,
                "nextSerial": self.serial}

    # ---------------------------------------------------------------- checks
    def validate(self):
        """(errors, warnings): validateScenario's SPEC 1.3 rules plus the SPEC 2.1 default-map distance bands."""
        err, warn = [], []
        n = len(self.objectives)
        if n == 0:
            err.append("no objectives")
        if n > 10:
            err.append("more than 10 objectives")
        xz = lambda p: (p[0], p[2])
        for k, o in enumerate(self.objectives):
            att = [s for s in self.spawns if s["role"] == "attacker" and k in s["phases"]]
            dfn = [s for s in self.spawns if s["role"] == "defender" and k in s["phases"]]
            if not att:
                err.append("phase %s has no attacker spawn" % o["label"])
            if not dfn:
                err.append("phase %s has no defender spawn" % o["label"])
            if k > 0:
                d = dist(xz(o["pos"]), xz(self.objectives[k - 1]["pos"]))
                lo, hi = SPEC21["objSpacing"]
                if not lo <= d <= hi:
                    warn.append("objective %s is %.0f m from %s (SPEC 2.1: %.0f-%.0f)" % (o["label"], d, self.objectives[k - 1]["label"], lo, hi))
            for s in att:
                d = dist(xz(s["pos"]), xz(o["pos"]))
                lo, hi = SPEC21["attackerBefore"]
                if d < 150:
                    warn.append("attacker spawn %s closer than 150 m to %s" % (s["label"], o["label"]))
                if not lo <= d <= hi and not (k == 0 and d > lo):
                    warn.append("attacker spawn %s is %.0f m before %s (SPEC 2.1: %.0f-%.0f)" % (s["label"], d, o["label"], lo, hi))
            for s in dfn:
                d = dist(xz(s["pos"]), xz(o["pos"]))
                lo, hi = SPEC21["defenderBehind"]
                if d < 80:
                    warn.append("defender spawn %s closer than 80 m to %s" % (s["label"], o["label"]))
                if not lo <= d <= hi:
                    warn.append("defender spawn %s is %.0f m behind %s (SPEC 2.1: %.0f-%.0f)" % (s["label"], d, o["label"], lo, hi))
                for a in att:
                    if dist(xz(s["pos"]), xz(a["pos"])) < 200:
                        warn.append("defender spawn %s within 200 m of attacker spawn %s" % (s["label"], a["label"]))
        return err, warn
