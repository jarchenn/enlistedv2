# Search and Destroy (SD1)

A round-based bomb-defusal mode: two persistent teams swap between Raiders (attackers) and Security (defenders). One life per round, an equipment economy, two bomb sites. The rules follow the user's specification ("Search & Destroy - implementation specification", 16 Nov 2026). Deviations are listed at the end of this file once the mode is complete.

## Map contract (for map authors)

A Search and Destroy map is an ordinary map file whose scenario (`doc.extra.scenario`, scenario version 2, as written by `tools/mapgen/mapgen/scenario.py` `ScenarioBuilder.to_json`) provides the following.

### 1. Mode

- `rules.mode = "snd"`. The map can still be started in another mode: Search and Destroy objects (sites, bomb) only exist while the mode is Search and Destroy.
- Optional `rules.sd` overrides the defaults below. Every key is optional:
  - `buyRadius_m` (12)
  - `callouts`: `[{name, x, z, r}]`, named areas the HUD shows as "your location", for example `{"name": "A Street", "x": 40, "z": -12, "r": 9}`. Recommended; not required.
  - `bounds`: `[[x, z], ...]`, a polygon of the playable envelope. A dropped bomb outside it, or off the navmesh, goes back to its last valid spot. Without it, the navmesh is the bound.
  - Format and timing overrides (`format`, `freeze_s` and so on). Leave these to the lobby.

### 2. Bomb sites A and B: exactly two objectives

- `objectives[0]` (order 0, label `"A"`) is Site A; `objectives[1]` (order 1, label `"B"`) is Site B. `displayName` is free text, for example "A Loading Yard". More than two objectives is a validation error.
- The objective's shape is the plantable area, not the whole site:
  - a circle `radius`, or a `polygon` (`captureShape: {"type": "polygon", "points": [[x, z], ...]}`);
  - `yMin` / `yMax` set the height band, relative to `pos.y`, that the planter's feet must be in.
  - Make it about 8-12 m across inside a site combat space of about 20-28 m, on flat, walkable, navmesh-covered ground that a defender can reach and stand on beside the bomb (the bomb must be defusable within 1.5 m).
  - Mark the plant area readably on the floor (decals / paint).
- `defenseSlots`: 2-4 per site, with priority. Defender bots hold these. Use positions with cover that see the plant area and are clearable by attackers.
- `routes`: attacker approach lanes per site, optional but strongly recommended so bots split. Give two lanes per site (`left` / `main` / `right`), each a list of points from outside the attacker spawn to the site edge.
- `captureTime_s`, `attackYaw`, `linkedStructures` are ignored by this mode. Set `attackYaw` to the main attack direction anyway, for the editor arrow.

### 3. Spawns

- Spawn markers with `role: "attacker"` (Raiders spawn) and `role: "defender"` (Security spawn); `team: -1`; `phases: [0, 1]` (phases are ignored by this mode).
- At least 5 per side (10 per side for a 10v10 custom game). Each needs at least 0.7 m of clear walkable ground and is at least 1.2 m from the next. Face them out of spawn. Leave `rack` empty (`rack=None` in `ScenarioBuilder.spawn`): there are no weapon racks in this mode, players buy instead.
- The two spawns must not see each other.
- After halftime the teams swap sides; the markers do not move. Attacker markers are always the Raiders' spawn.

### 4. Buy zones

- The buy zone of a side is everywhere within `buyRadius_m` (default 12 m, horizontal) of any of that side's spawn markers, within 3 m of height.
- Keep each side's five markers inside its spawn courtyard so that this union covers the courtyard. A buy zone must not reach a site or the mid area.

### 5. Bomb

- No marker. At round start the bomb goes to a random living attacker (spec §5).
- A dropped bomb must land on recoverable, navmesh-covered ground. Anything else returns it to its last valid ground position (spec §5).

### 6. Geometry rules the engine relies on

- A navmesh over all playable ground. The game bakes one at load, or saves it with the map. It is used for:
  - the bots;
  - bomb recovery;
  - the validation "site reachable from both spawns".
- Doors on principal routes start open, or are removed. Their state at load is restored each round.
- Structural destruction is off in this mode: the bomb and grenades damage players only, and no crater or wall breach changes a route. Glass and small breakables are restored each round.
- No vehicles, AT guns, weapon racks, loot spots or artillery are needed. Any that are placed are inactive in this mode.

### 7. Recommended size and timings (spec §12)

| Item | Target |
|---|---|
| Playable envelope | about 180 x 160 m (±20 %) |
| Fights | short 5-15 m, medium 15-30 m, a few long 35-55 m lines; no spawn-to-spawn sightline |
| Doors / lanes | combat doors 1.4-1.8 m; main lanes 3-6 m; secondary corridors 2-3 m |
| Cover | low 0.9-1.1 m; full at least 2 m |
| Site combat space | 20-28 m across; plant area 8-12 m |
| Defenders spawn to site cover | 6-9 s at knife speed 5.5 m/s (about 33-50 m) |
| Attackers spawn to first contested boundary | 10-14 s (about 55-77 m) |
| Attackers to a plant area, shortest uncontested route | 15-20 s (about 83-110 m) |
| Attackers through mid to a site | 18-25 s |
| Site to site, defender rotation | 10-14 s |
| A to B through mid | 8-11 s |
| A to B on the attacker-side fallback | 18-24 s |

Defenders should reach site setups about 3-5 s before a full-speed attacker rush. Speeds in this mode are knife 5.5, rifle about 4.6, walk 2.5 and crouch 1.8 m/s; sprint and prone are off.

### 8. Validation

The game refuses to start the mode on a map with:

- fewer than 5 spawns per side;
- spawns of the two sides closer than 40 m;
- a missing site;
- more than two sites;
- a site plant area with no navmesh;
- a site not reachable on the navmesh from both spawns.

`--selftest-snd <map>` runs bot-only rounds on a map and prints the route timings above, measured from each spawn.

### 9. Temporary markers

Until the dedicated maps exist, the mode runs on `templates/chateau_town.nmap.json` with built-in temporary sites and spawns (`src/sd_mode.cpp`, `sdTempLayout`). These are used only when a map has no Search and Destroy scenario.
