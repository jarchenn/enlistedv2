# Abilities: explosion pack, artillery, suppression (GM2)

Code: `src/abilities.h`, `src/abilities.cpp`. Numbers are game tuning.

## Grants

Items work from a weapon id and abilities from an ability id. The class system (GM1) grants them per class; until then a debug grant gives them.

| Id | Kind | Class (GM1) |
| --- | --- | --- |
| `explosion_pack` | Weapon (`content/weapons/explosion_pack.json`) | Heavy: 2 |
| `artillery` | Ability | Ranger / Fallschirmjäger |

- **Local player:** `gm2Grant(app, id, count)`. A weapon id joins the carried stack with `count` of it; any other id is an ability.
- **Debugging:** `NMK_GM2_GRANT=explosion_pack,artillery` grants both at the start of an Explore session.
- **Host, other soldiers:** `gm2GrantSlot(slot, "artillery")` lets that slot's LAN requests through. A remote soldier's packs come from its `carried` list and `wsBy` counts, as for any weapon.
- The pack is never in the default loadout.

## Explosion pack

- In hand, left click places one on the surface in reach: 2.2 m along the view, on a wall, a vehicle or the ground. Nothing in reach: nothing is placed and the count stays.
- It sticks where it lands. On a tank it rides along with the hull.
- **Fuse:** 10 s after placing, it goes off once through the shared `explode()` path: structural damage, crater, glass, props, tanks. Soldiers take damage through the same blast hook as other explosions, attributed to whoever placed it.
- **Tuning** (`pack` block): damage 450, radius 6 m, crater 0.4, reach 2.2 m.
- Drawn as a canvas satchel with a fuse box (box model; no supplied model).
- **LAN:** a client's placement is a request. The host checks the soldier carries one, places it from that soldier's eye, and sends `gm2pack` so clients draw it. The blast reaches clients as the normal `ex` event.

## Artillery

- **Key 7:** opens the artillery map. It is the minimap of the whole terrain, with the cursor free.
- **Targeting:** a click sets the target, shown as a red crosshair plus the barrage's spread. **Call in** fires.
- **Barrage:** after a 4 s delay, 6 shells land 0.55 s apart, scattered within 14 m of the target. Each shell lands on the first surface below it (roofs, bunkers) and goes through `explode()` (damage 300, radius 7 m, crater 0.45).
- **Cooldown:** 90 s, shown on the map ("Ready in N s").
- **Host side:** strikes run on the host or in solo. A client's call is a request; the host checks the slot's ability and its own cooldown, schedules the strike and sends `gm2arty` ("Artillery incoming").

## Suppression vignette

- **Near miss:** an enemy round whose path passes within 2.5 m of the local player's eye counts once. On a LAN client, the host's tracer ghosts count too.
- **Darkening:** each round darkens all four screen edges by 0.12, plus up to 0.18 more the closer it passed (level 0 to 1). A burst therefore darkens it quickly.
- **Fade:** 0.35 s after the last round it fades at 0.9 a second, so about a second from full.

## Tests (`--selftest-weapons`)

- **Pack grant:** a weapon id, 10 s fuse, not in the default loadout; a grant gives 2.
- **Pack, nothing in reach:** aimed at the sky, nothing is placed and the count stays 2.
- **Pack placed:** on the ground in reach. Nothing happens before 9.9 s; it goes off exactly once by 10.6 s, leaves a crater and one is left.
- **Artillery:** refused without the ability. Once granted, the call is accepted and a second call is refused (cooldown). No shell lands before the delay, then all 6 land within the spread.
- **Suppression:** 5 rounds past the eye count 5 near misses. One round gives a light vignette, a burst of 4 is clearly darker, and 2 s later it has faded.
- **Minimap:** the screen ↔ world mapping round trip and the whole-terrain fit.
