# Applying the weather and particles corpus

Second volume: `Documents\MapMaker\corpus\weather-and-particles\` (9 chapters, about 130,000 words), read in full on
2026-11-20 (08 and 09 by the coordinator, 01-07 by three readers who checked each idea against `src`). The user's
requests: "weather and partciles corpus for you as well" and "add in all the new weather patterns to menu as well."
It follows `docs/corpus-implementation.md` (first volume) and uses its light units and exposure.

The corpus's own caveat applies: its constants are from the literature or from recall, every millisecond figure is an
estimate for older hardware, and no code in it was compiled. Every item below is measured here before it is kept.

**The argument in one paragraph.** All weather is one equation: the surface dimmed by the air, plus the light the air
scatters toward the eye. Most of the impression of weather comes from four nearly free things: light level and
exposure, density and colour of the air, how surfaces respond (wet, snow, dust), and wind moving everything together.
Particles are the near-field accent and come last. Bad weather hides distance and removes the sun, so a system that
culls by visibility and drops sun shadows under overcast makes a storm cost no more than a clear day. Every effect
reads one shared weather state.

**Priority.** The user's standing order is optimisation first. So: the cost-saving items (section 3) go into the
optimisation work now; the menu with all presets at the cheap level (sections 1, 2) is a direct request and is built
next; the accents (sections 4, 5) wait behind the optimisation runs.

## 1. The menu: fifteen presets

Settings > Display > Weather today offers Clear, Cloudy, Map default (`src/settings.cpp:471-475`, state `R.wx` in
`src/render.h:129-134`). It becomes the corpus's preset table (08 section 3.4) plus Map default. Saved values keep
their meaning: 0 stays Clear, 1 (was "Cloudy") is Overcast, new ids are appended; the menu shows them in the order
below.

| id | Preset | Cover | Cloud τ | Visibility | Precipitation | Wind m/s (gust) | Diffuse lux | EV100 | Sun shadows |
|---|---|---|---|---|---|---|---|---|---|
| 0 | Clear | 0.1 | 0 | 40 km | none | 3 (1.3) | 15,000 | 15 | hard |
| 2 | Haze | 0.2 | 0 | 7 km | none | 2 (1.2) | 25,000 | 14.3 | soft |
| 3 | Broken cloud | 0.5 | 30 | 30 km | none | 5 (1.4) | 25,000 | 14.5 | moving cloud shadows |
| 4 | Light overcast | 1.0 | 10 | 20 km | none | 4 (1.3) | 50,000 | 14.2 | very soft |
| 1 | Overcast | 1.0 | 30 | 15 km | none | 5 (1.4) | 22,000 | 13.1 | none |
| 5 | Drizzle and mist | 1.0 | 50 | 2.5 km | drizzle 1 mm/h | 3 (1.3) | 11,000 | 12.1 | none |
| 6 | Steady rain | 1.0 | 100 | 2.5 km | rain 10 mm/h | 7 (1.5) | 7,500 | 11.5 | none |
| 7 | Thunderstorm | 1.0 | 300 | 800 m | rain 50 mm/h, lightning 6/min | 14 (1.6) | 2,500 | 9.9 | none, lightning only |
| 8 | Dawn ground fog | 0.1 | 0 | 150 m in a 5-50 m layer | none | 0.5 (1.1) | bright fog | 10-13 | shafts |
| 9 | Dense fog | 1.0 | 30 | 90 m | none | 1 (1.1) | 7,000 | 11.3 | none |
| 10 | Light snow | 1.0 | 30 | 2 km | snow 1 mm/h | 2 (1.3) | 27,000 | 13.3 | none |
| 11 | Blizzard | 1.0 | 100 | 250 m | snow 4 mm/h | 15 (1.6) | 10,000 | 11.8 | none |
| 12 | Clear day on snow | 0.1 | 0 | 30 km | none, snow lying | 3 (1.3) | 20,000 + bounce | 15.7 | hard, blue |
| 13 | Blowing dust | 0.2 | 0 | 3 km (dust) | none | 9 (1.5) | 25,000 warm | 14.2 | soft |
| 14 | Sandstorm | 0.3 | 0 | 100 m (dust) | none | 18 (1.6) | 12,000 orange-brown | 12 | none |

Lux, EV and visibility are the corpus's ranges at their midpoint; the wind column is the coordinator's starting
choice from the Beaufort table in chapter 01. All are starting values.

Menu rules:
- Every preset is offered on every open-sky map. Enclosed maps (Tunnels, Backrooms) keep none, as now.
- A preset never changes the hour; Time of day stays separate. The tooltip for Dawn ground fog says it is meant for
  dawn.
- Each preset must work at the cheap level first (section 2). Where a map lacks the surface layer a preset wants
  (lying snow on a summer map, sand on a green map), the preset still changes light, air, falling particles and wind;
  lying snow and drifting sand appear only on maps whose look enables them, until accumulation (section 5) exists.
- LAN: the host's weather applies, as now. Visibility is identical on every quality preset and is the same number the
  bots' sight uses (08 section 10).
- "Environmental fog" (hollow fog amount) stays; it no longer sets how far you can see. Visibility comes from the
  preset.

## 2. One weather state, and the cheap level (stage W1)

Replace `Renderer::Weather`'s single 0..1 `k` with the corpus's state (08 section 2): cloud cover, cloud optical
depth, cloud base, visibility, fog top and scale height, precipitation type and rate, wind direction, mean, gust
factor and period, temperature, dust visibility, lightning rate; plus accumulated wetness, puddle level, snow depth,
dust cover. A preset is a row of targets; the current state moves toward it with per-field rates. Derived each frame
and shared by every system (one uniform block; CPU copy for AI, sound, particles):

- Direct sun `E_direct = E_DN ((1 - c) + c exp(-τ / μ0))`; cloud diffuse `T_diff = (2/3 + μ0) / (4/3 + (1 - g) τ)`,
  g 0.85; sky `E_sky = (1 - c) E_clear + c E_cloud`; ground feedback `1 / (1 - ρ_ground c (1 - T_diff))` (snow, sand).
- Colour: neutral 6,500-7,500 K under full overcast, no warm-sun and blue-shade split. Dust tints both.
- Extinction adds: `σ = 3.912 / V + 0.31e-3 R^0.67 + 3.9e-3 S + 3.912 / V_dust`. Fog colour
  `L_fog = albedo (E_direct p(θ, g) shadow + L_ambient)` with a forward phase (g 0.8-0.9 fog, 0.7 dust), recomputed
  every frame, so fog always matches the sky at the horizon.
- Exposure follows `EV100 ≈ log2(E_total / 2.5)` with two-thirds compensation: a storm ends darker on screen than a
  sunny day; snow and fog are exposed up, not down.
- `sunShadowStrength` fades to 0 as direct light falls under 5 % of the total; `cullDistance = 1.42 V`.
- Wind: one direction and speed in m/s for grass, foliage, flags, smoke, snow, rain and sound, with one gust map
  `1 + G noise((x - W t) / L)`, L 20-100 m, moving downwind as fronts.

At this level a preset costs nothing extra and already reads as its weather. This is what the menu ships with.

## 3. What weather saves (goes into the optimisation work now)

1. No sun shadow cascades when direct light is under 5 % of the total (every overcast, rain, fog, snow and sandstorm
   preset, and Blackwater Thaw today). Keep AO and contact shadows. Fade, do not snap.
2. Cull, LOD-bias and shorten shadow, vegetation and probe distances by `1.42 V`, with a dithered fade over the last
   10-20 %. Lamps are the exception: keep them as fogged glows to about 3.2 V.
3. Under uniform overcast and inside fog the sky is a cheap gradient, not the cloud march; gate the light-shaft march
   off when there is no direct sun.
4. Screen-space reflections: stop the march at 0.75 V. In rain keep wet-ground roughness above 0.2-0.25 except in
   puddles, or reflections become the storm's largest cost.
5. The snowfall occlusion map is 2048 x 2048 over ±90 m, redrawn every 16 frames; the corpus default is 512 over 64 m.
6. Particles (`src/fx.cpp`): near-camera fade and a screen-size cap for every alpha sprite (today only additive ones
   fade). A smoke grenade is 70 quads about 5.5 m wide, up to 6 clouds: tens of screens of overdraw with the camera
   inside. Hand the inside of a cloud to the fog term (the gameplay optical depth is already analytic,
   `grenade.cpp:40-54`). Replace `parts.erase(parts.begin())` at the cap with a ring. Measure with `GP_Fx`.
7. Covered ground is cheaper than bare ground: where snow cover is about 1, skip the substrate's texture taps; bury
   grass by `saturate(1 - snowDepth / bladeHeight)`.

Not the cause of the moving-camera hitches: particle uploads are under 0.6 MB a frame. The multi-MB uploads are
elsewhere (grass chunk rebuilds are a suspect, unverified).

## 4. Accents, in the corpus's order (after the optimisation runs)

**W2 fog and cloud corrections** (lighting).
- Overcast sky gradient is inverted: zenith should be about three times the horizon. Fog colour must equal the cloud
  deck at the horizon.
- Haze by visibility: today σ = 0.0010 clamp(1200 / renderDist, 0.4, 2) (`settings.cpp:253`), so High sees twice as
  far as Low. It becomes the preset's visibility on every tier.
- Valley mist without the 0.3 floor (`render.cpp:5984`); density that varies 2-3x over tens of metres so fog does
  not read as a filter; a 5-50 m layer for Dawn ground fog.
- Exposure raised under fog and snow (`lighting.cpp:537` multiplies the key by 0.72).
- Cloud field with structure, on a shell not a plane; cloud-shadow contrast and scale for Broken cloud; exposure must
  not chase each cloud shadow.

**W3 wet, snowy and dusty surfaces** (materials, map).
- Wetness in two stages: a film (roughness down, darkening by porosity) then soak and puddles against the height
  maps; ripples from a 256 x 256 texture; drying after the rain. The matte-ground floor must not override it
  (first volume, step 3).
- Snow: neutral albedo 0.82-0.88 (today (0.80, 0.83, 0.88) is blue), edges blended by height, a dark wet halo and a
  contact band at the snow line, AO and bounce using snow's albedo, trodden snow on the traffic mask.
- Sand and dust cover: slope and wind-shelter masks, per-object dust accumulation.

**W4 precipitation** (map session's snowfall path, generalised).
- There is no rain today. Minimal design: stateless streak volumes (8,000 near + 8,000 far on Medium), slant
  `atan(wind / fall)`, length `v t_exposure`, sub-pixel width moved into alpha; the existing top-down occlusion map
  so it stops under roofs; splashes up to 1,000; sheets and the rain's own extinction beyond. Estimated 0.4-1.0 ms.
- Snowfall bug: position is `p0 + vel * uTime` with a gust-scaled velocity (`render.cpp:3724-3774`), so sideways
  speed grows with session time. Accumulate offsets on the CPU; wrap time; dots 4-12 mm, never streaks; lit and
  fogged per vertex. Blizzard: near-horizontal flakes plus spindrift at ground level.
- Lightning: a brief directional light and sky flash from the storm's rate, with thunder delayed by distance.

**W5 dust and sandstorm** (terrain, lighting).
- `sd_desert` has no `look.arid`; only `outpost_desert` does. Add it.
- Always on in the desert: haze by visibility with a warm albedo, a 1-3 m ground layer whose density follows
  `drive(u) = clamp(((u - u_t) / (u_full - u_t))^3, 0, 1)` (u_t 5-6 m/s, hysteresis at 0.8 u_t), heat shimmer beyond
  80 m on clear afternoons (about 0.1 ms).
- Blowing dust: streamers in the terrain shader within 40-60 m (3 noise taps).
- Sandstorm: wind leads the fog by 5-15 s; visibility ramps geometrically to the core; sun through dust
  `exp(-τ m_rgb / sin e)` with the energy moved into a tinted ambient; gust-modulated fog; optional 3-5 shells at
  half resolution; 1,500-5,000 stateless streaks. The interior is fog, never sprites. Estimated 1.1-1.8 ms, repaid
  by culling at 1.2 V and no cascades.

**W6 particle engine and ambient life** (gameplay, vegetation).
- Fog on every particle per vertex (free; the corpus says never cut it); per-vertex lighting with a sun-shadow tap;
  soft fade of 0.35 x radius from the depth copy. Today `kFxFS` is unlit, unfogged and has no depth fade.
- Event dust: colour from the ground albedo at spawn, gated on wetness, opacity `1 - exp(-τ0 (R0 / R)^2)`, a merge
  rule for automatic fire, footstep puffs on arid maps (cap 24, under 30 m).
- Ambient life by the stateless path snowfall already uses (zero upload): outdoor motes and pollen (500-3,000
  points), drifting leaves in the forest (800 Medium, 2,000 High), embers over fires. Indoor motes move off the CPU.
- Overdraw budget by predicted screen area; a half-resolution buffer for large soft sprites only if still over
  2 screens.

## 5. Dynamics (last)

Changes arrive in the natural order (08 section 5): wind and cloud first, light next, precipitation after, surfaces
last, and drying far slower than wetting. First-order rates per field; accumulation equations for wetness, puddles,
snow depth and dust cover. Fronts crossing the map and scheduled weather per match are later options; a menu change
may snap within a few seconds until then.

## 6. Who does what

| Session | Now (saves cost) | Next (menu) | Later (accents) |
|---|---|---|---|
| Lighting | 3.1 no sun shadows without direct sun, 3.3 sky and shaft gates | The weather state, the 15 presets in the menu, light, air colour and exposure per preset (W1) | W2, lightning, sandstorm light |
| Performance (owns sound) | 3.2 cull by visibility, 3.4 SSR range, worst-weather benchmark row | | rain, wind and thunder sound per preset from the shared state |
| Gameplay | 3.6 particle near fade, cap ring, grenade fog hand-off | bots' sight from the preset's visibility | W6 particle lighting and event dust |
| Map | 3.5 occlusion map size, 3.7 snow cost, snowfall bug (in SN2) | | W4 rain and blizzard on the snowfall path |
| Materials | | | W3 wetness, puddles, ripples, dust cover |
| Terrain | | `look.arid` on `sd_desert` | W5 streamers, shimmer |
| Vegetation | | grass and foliage read the shared wind | W6 leaves, pollen |

## 7. Verification

- Per preset, on one open map and one town map, same camera: a sheet of all fifteen, with measured sun:shade stops,
  mid-grey display value, visibility (distance at which a black target falls to 2 % contrast) and GPU time.
- Fairness: the measured visibility is identical on Low, Medium and High and over LAN.
- Cost: no preset costs more than Clear on the same view; Thunderstorm in the heaviest town view with combat effects
  is the benchmark's worst-case row.
- Snowfall: flake speed on screen at 30 minutes equals flake speed at 1 minute.
- The user judges each preset in play; nothing is play-tested by the coordinator.

## Appendix: engine facts the readers recorded

- Weather: `R.wx` (`render.h:129-134`), `cloudCoverNow` mixes to 0.86 at Cloudy (`render.h:135`); menu
  `settings.cpp:471-475`; `settingsUiWeather` `settings.cpp:333-335`.
- Haze: `settings.cpp:253`; env-fog field with drifting noise `render.cpp:273-315`; mist floor `render.cpp:5984`;
  overcast key x0.72 `lighting.cpp:537`.
- Snowfall: `kSnowfallVS` `render.cpp:3728-3765`, counts from the map look (Blackwater Thaw 40 near, 1,800 far),
  scaled 0.4 / 0.75 / 1.0 by cloud quality (`settings.cpp:254`); unwrapped float time (`render.cpp:5017`); snow wind
  (0.8, -0.6) with its own gust sine (`render.cpp:5498-5500`).
- Wind: vegetation uses a fixed direction (0.8, 0, 0.6) with `uWind` 0 or 1 (`render.cpp:3172-3173, 3268-3270`,
  `grass.cpp:531`); no m/s value, no shared field; snow wind disagrees in z sign.
- Particles: CPU, `combat.h:154-165`, `fx.cpp`; caps 360 / 540 / 900 / 1,350 particles by preset
  (`fx.cpp:165-168`); 216 bytes per particle uploaded twice a frame (`render.cpp:5479, 5484`); drawn after TAA and
  bloom, unsorted (`render.cpp:5465-5488`); `kFxVS` / `kFxFS` `render.cpp:3775-3824`; near fade additive only
  (`fx.cpp:253-257`).
- Smoke grenade: `grenade.h:16`, `grenade.cpp:40-71`. Vehicle dust `tank_sim.cpp:1183-1193`; rotor
  `heli.cpp:375-385`; impacts `fx.cpp:205-209`; no footstep dust.
- Desert: `uMapArid` paints sand in the terrain albedo only (`render.cpp:1745-1749`); no haze layer, streamers,
  shimmer or storm.
- Indoor motes: `lights.cpp:84-160`, built on the CPU.
- Half-resolution targets exist for AO and shafts (`render.cpp:~2444, 4095-4104`), none for particles.
