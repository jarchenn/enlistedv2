# Map ambience (BTA)

Any map can carry its own ambience as data: `content/audio/map_ambience/<template id>.json`, overlaid key by key by the map's
own `extra.audio` (the map wins). Driven by `src/map_ambience.cpp` every frame in first person; silent in the editor and
on the menu. Positions are engine x / z; heights follow the terrain (plus `y`). Files are under `content/audio/custom/`.
All levels sit on the **Ambience** volume, ducked under nearby gunfire like the beds; `mix.json` `map_ambience` trims
them by category.

| Key | Meaning |
|---|---|
| `battlefield`, `bed`, `bedVolume` | As before (CV1): the distant battle bed on or off, a bed file of the map's own |
| `room` | The gun reverb's room, 0 open .. 1 enclosed (default: 0 with the battlefield bed, 1 without) |
| `wind` | `{file, volume, underRoof}`: the steady bed everywhere. It swells with the gusts (`mapWindGust()`) and drops to `underRoof` x under a roof (a ray straight up, smoothed over ~0.4 s) |
| `zones[]` | `{id, at: [x, z], r, fade, wind, leaves, gust: {file, volume, underRoof}}`: full weight within `r`, gone `fade` m further. `wind` scales the bed, `leaves` raises the procedural leaf rustle, `gust` is a bed of its own that follows the gusts |
| `loops[]` | `{id, file, at: [x, z] or path: [[x, z], ...], y, volume, min, max, underRoof, cat}`: a looping point source (mono, spatial: full level within `min`, silent at `max`). A `path` voice rides the point of the line nearest the listener (a creek). `cat` picks the mix.json trim (`water` default, `fire`) |
| `creaks[]` | `{at, r, every: [lo, hi], volume}`: wood creaks (`amb_creak`) now and then while the listener is within `r`; more often and louder in gusts |
| `surfaces` | `{paint id: step event}`: the footstep of each ground-paint type on this map (`grass` = unpainted ground) |

**Footsteps.** `App::stepSound` asks `mapStepEvent`: six samples round the foot (the centre counted twice, four 0.35 m
out), each first the map's snow mask (`snowStepAt`, BT1 `src/snow.h`: snow, deep snow, slush, meltwater; declared weak, so
it is simply skipped until that code is linked), else the dominant painted type (under 35 % paint: the unpainted
ground), mapped through `surfaces` or the type's own `step`; the most votes wins, so a transition dithers over about 0.7 m instead of switching
on a line. Water from 5 to 30 cm deep is `step_shallow`, deeper `step_water`. Winter surfaces: `step_snow` (powder
crunch), `step_snow_deep`, `step_slush`, `step_mud`, `step_shallow` (`custom/bta/steps_*.mp3`, levels in mix.json
`steps`). Shallow water and slush steps leave one or two ripple rings at the contact, from the gore puff pool (capped
at 600), for the player and for bots within 30 m.

Blackwater Thaw's file places the wind bed, gusts on the quarry overlook (and lighter on the washhouse terrace), a
calmer court and workshop yard, the orchard's rustle, the creek along its whole line plus the stone bridge, footbridge
and ford, the waterworks flume (head to foot), two drains, the smithy hearth and creaks at five old sheds, at the
anchors BT1 writes to `content/templates/blackwater_thaw.audio.json`; the pebble creek shore steps as stone.
