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
- The objective's shape is the **plant pad**, a small area inside the site, not the whole site (SD3):
  - a 3.5 x 3.5 m square `polygon` (`captureShape: {"type": "polygon", "points": [[x, z], ...]}`), or a circle of `radius` 1.75 m. Keep it between 3 and 4 m across;
  - `yMin` / `yMax` set the height band, relative to `pos.y`, that the planter's feet must be in (the temporary layouts use -1 / +1.5 m);
  - put it on flat, walkable, navmesh-covered ground that a defender can reach and stand on beside the bomb (the bomb must be defusable within 1.5 m). `pos` must be on the navmesh: the game moves a pad whose centre is off it onto the nearest walkable point (up to 3 m) and logs it.
  - The game draws the pad on the ground: hazard-striped border, corner brackets, the site letter (upright for attackers coming from their spawn). It glows while the bomb carrier stands inside. The drawn outline is the plant test itself, so no decals are needed.
- The **site** is the area around the pad (about 20-28 m across) where the fight happens: the defense slots, the callouts and the bots' site positions (up to about 12 m from the pad). It needs no shape of its own.
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
- No vehicles, AT guns, weapon racks, loot spots or artillery are needed. Vehicles placed on the map are removed for the match unless the match option "Vehicles" is on (SD3; off by default). The others are inactive in this mode.

### 7. Recommended size and timings (spec §12)

| Item | Target |
|---|---|
| Playable envelope | about 180 x 160 m (±20 %) |
| Fights | short 5-15 m, medium 15-30 m, a few long 35-55 m lines; no spawn-to-spawn sightline |
| Doors / lanes | combat doors 1.4-1.8 m; main lanes 3-6 m; secondary corridors 2-3 m |
| Cover | low 0.9-1.1 m; full at least 2 m |
| Site combat space | 20-28 m across; plant pad 3-4 m (SD3) |
| Defenders spawn to site cover | 6-9 s at knife speed 5.5 m/s (about 33-50 m) |
| Attackers spawn to first contested boundary | 10-14 s (about 55-77 m) |
| Attackers to a plant area, shortest uncontested route | 15-20 s (about 83-110 m) |
| Attackers through mid to a site | 18-25 s |
| Site to site, defender rotation | 10-14 s |
| A to B through mid | 8-11 s |
| A to B on the attacker-side fallback | 18-24 s |

Defenders should reach site setups about 3-5 s before a full-speed attacker rush. The spec's speeds were knife 5.5, rifle about 4.6, walk 2.5 and crouch 1.8 m/s. The game's own speeds apply instead (deviation 6): jog about 2.7 and sprint about 5.4 m/s, with sprint and prone on.

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

## Playing it

- **Start:** Play > match setup > "Search and Destroy". The defaults are 5 v 5 soldiers on two persistent teams. Options:
  - "Short match" (MR12);
  - "Warmup (60 s)", off by default (SD3; `search_destroy.json` `"warmup"`);
  - "Vehicles", off by default (SD3; `search_destroy.json` `"vehicles"`): off, the map's vehicles are taken out of the world for the match (and put back after it; saving the map during the match still writes them) and none spawn;
  - friendly fire (on by default, spec §11).
  Any map works: one without an authored layout gets the temporary one (§9).
- **Round flow:** freeze 15 s (frozen; buy with **B** inside your spawn), then 1:55 live with 20 more seconds of buying.
- **Buy menu (B):** "Buy Time Remaining" on top; five numbered columns (1 Equipment, 2 Pistols, 3 Mid-Tier, 4 Rifles, 5 Grenades) of numbered cards with the item's amber silhouette and its price (green; red when you cannot afford it; owned cards greyed with "OWNED"). Press a column number then an item number, or click a card; right click refunds this round's purchase; B or Esc closes it.
- **Planting:** the bomb carrier holds **E** on a site's plant pad for 3.2 s. Attackers see each pad's letter and distance on screen; "Hold E to plant" shows only while the carrier stands on the pad.
- **After the plant:** a 40 s fuse. Defenders hold **E** within 1.5 m of the bomb: 10 s, or 5 s with a kit.
- **Commands:** `/sd skip` (end the warmup), `/sd timeout`, `/sd buy <id>`, `/sd buymenu`, `/sd state`.
- **Data:** every rule value, reward, price and the shop are in `content/settings/search_destroy.json`.

## Implementation

- **`src/sd_mode.{h,cpp}`:** the authoritative round controller, pure logic. It has:
  - the states Warmup / Ready / Freeze / LiveUnplanted / LivePlanted / RoundEnd / Halftime / OvertimeBreak / MatchEnd / Timeout;
  - the clocks;
  - the result precedence and tie policy: plant before the deadline, defuse before the detonation (a tie detonates); objective resolution before eliminations; one latched result;
  - the bomb;
  - the economy: rewards, the loss ladder, the timeout rule, the planted-loss bonus;
  - buying and refunds, MR15 / MR12 and MR3 overtime blocks.
  `sdLogicSelfCheck` covers the spec's edge cases.
- **`src/sd_game.{h,cpp}`:** the game layer:
  - the temporary layout;
  - the round reset (everyone respawns at the side's markers, survivors keep what they carry);
  - gear and the free pistols;
  - side and uniform swaps;
  - plant / defuse holds;
  - the planted bomb, drawn as the explosion pack with its LED and accelerating beeps;
  - the 500 / 35 m personnel-only blast;
  - armor (spec §8 formula);
  - kill rewards;
  - bot goals and buying;
  - HUD, kill feed and buy menu;
  - LAN.
- **LAN:** the host owns everything. Clients receive the state (`"sd"` events, on every change and at least every 0.5 s; `"sdboom"` for the blast) and send `"sdhold"` / `"sdbuy"` requests. `kNetVersion` is 18 (SD3: the Vehicles rule).
- **`--selftest-sd [map]`:**
  - the logic self-check;
  - bot rounds 5 v 5, one per win condition (defenders eliminated, attackers eliminated, time, a bot plant that detonates, a bot plant a defender bot defuses);
  - halftime (sides swap, $800);
  - a free bot round.
- **`--selftest-sd3`:** the Warmup / Vehicles defaults; a map vehicle gone for the match and back after it; the pads (3.5 m, on the navmesh, drawn outline = plant test); the carrier killed (its gun drops, the nearest attacker fetches the bomb); plant discipline (an enemy in view stops the plant, it resumes once clear); carry-over (a dead winner keeps its gun, a dead loser not); a capture of the buy menu (`selftest_out/sd_buymenu.png`).

## Deviations from the specification

These are changes from the spec or partial implementations, with the reason for each. Everything else follows the spec's values and rules (all of them data in `search_destroy.json`).

1. **The bomb is not a weapon.**
   - There is no objective-slot key 5, no carried model and no first-person plant animation.
   - The carrier plants with the spec's contextual hold-Interact (E) inside a plant area; holding primary fire does not plant.
   - G drops it, and death drops it. An attacker picks it up by walking over it (within 1.3 m) instead of with a separate interact.
   - The planted bomb is the explosion-pack model with its LED and accelerating beeps.
2. **Spectating** after death uses the existing third-person orbit camera over living teammates (enemies never), not a first-person view. There is no key to cycle between teammates yet.
3. **Shop:**
   - **Missing, because the game has no such weapon (never sold under another name):** the Raiders' high-capacity pistol (Tec-9 role), both automatic shotguns, the flashbang and the decoy.
   - **Defenders' incendiary:** it is the game's Molotov, at the defenders' price.
   - **Weapon stats:** the existing per-gun damage, handling and ADS are kept. The spec's per-weapon fields (armor penetration, kill reward, movement modifier and so on) are not on the weapons. Armor penetration and kill rewards come from the shop role (P: pistol / SMG 0.6, rifle / MG 0.775, shotgun 0.8, sniper 0.95; blasts 0.5).
   - **ADS:** still available on every gun, where the spec wants hip fire only for unscoped rifles.
   - **Menu:** no quick-buy favourites or rebuy button.
   - **Refunds:** only the last purchase can be refunded (that is always unambiguous). Use of a gun (firing) does not block a refund; thrown grenades and picked-up guns do.
4. **Drops on death:** everyone, player or bot, drops the gun in their hands (SD3). Nothing else drops. Only the local player can pick guns up (existing E pickup); bots do not pick up guns.
5. **Utility:** the game's existing frag, smoke and Molotov behaviour and timings, not the spec's HE 98 / 6 m, smoke 7 m for 18 s, or fire 40 dps. HE and fire still damage walls (only the bomb is personnel-only).
6. **Movement:** the game's controller with sprint (stamina) and prone on (the user's choice; the spec turns both off), mantling on. Every speed is `movement.json` `infantrySpeed_x` (0.85) x `search_destroy.json` `speed_x` (1.18): jog 2.70, sprint 5.39, crouch 1.13, prone 0.56 m/s for the player; bots alike. Bots sprint when they push or rotate without contact.
7. **Radar:** no teammate-sighting radar (enemy dots for 3 s, the dropped bomb for defenders). Attackers see the dropped bomb on screen and the carrier's name on the HUD.
8. **Disconnects:** the existing rule: the slot keeps playing as a bot (inventory kept) rather than "10 s under bot control, then removed as a death". A late joiner takes a bot slot at the next round start.
9. **Buy zone:** 12 m around the side's spawn markers instead of an authored volume (map contract §4).
10. **Map validation:** the spawn and site counts only. With anything missing, the temporary layout is used and a warning is shown. Reachability and defuse-access checks are not implemented; route timings are measured by the bot test, not checked.
11. **Explosion cover:** the bomb uses the engine's blast occlusion rather than a fixed 0.25 multiplier for full cover.
12. **Timeouts:** `/sd timeout` (two per team) starts a 30 s pause before the next freeze. There is no UI button.
13. **HUD:** inline panels in the current HUD style; the buy menu is its own glass panel after the user's reference layout (this branch has no shared glass-panel helpers). Equipment icons are silhouettes of the BR vest and helmet models; the defuse kit is a drawn glyph (there is no kit model).
14. **Debug:** debug output is `/sd state` and the log only. There is no debug overlay of deadlines, loss indices, plant legality or timings.
15. **LAN clocks:** clients draw the host's clocks from state updates sent at least twice a second, so a client's countdown can lag the host by up to 0.5 s. The host alone decides results.
16. **Bots (SD3):**
    - They buy by budget.
    - **Attackers:** one site per round. Two groups (alternate attackers) take two routes (the site's authored routes, else two lanes either side of the direct line) to staging points about 16 m out, wait there until every living attacker is staged (or 30 s after the first, or under 50 s on the clock), then go in together. Outside contact they sprint; in contact they fight cover to cover.
    - **The bomb:** if the carrier dies, the attacker nearest the bomb goes for it at once, sprinting, contact or not.
    - **Plant discipline:** the carrier plants only with no enemy in view and not hit in the last 2 s. Engaged, it lets go (the plant resets), fights, and plants again once clear.
    - **Defenders:** about 60 % hunt in pairs (rounded; at least two stay back): forward along the attackers' approach, the partner with its leader, onto anything the pair has seen in the last 12 s. The rest hold the two sites' defense slots. After a plant, everyone converges on the bomb and the nearest defuses. They sprint when rotating outside contact.
    - No grenade lineups or saving decisions. Perception, smoke and visibility are the existing AI's.
17. **Carry-over (SD3, the user's rule):** the winning side keeps its guns for the next round, its dead included; the losing side's dead lose theirs (survivors keep what they carry, as in the spec). Armor, helmet, kit and grenades are lost on death either way. The spec has every death lose everything.
