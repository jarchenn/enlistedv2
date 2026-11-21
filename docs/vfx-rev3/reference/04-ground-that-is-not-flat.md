# 04. Ground that is not flat

The supplied house's ground is one hand-painted 2048² map at 324 px/m over a near-planar mesh with no second layer, no height, no parallax, no scatter and no contact geometry. The supplied dirt-road scene solves the same problem with 26 materials stacked as overlapping meshes. This chapter is the diagnosis, the maths of each fix, and the cost of each.

## 4.1 Diagnosis: the six cues, and how to measure that they are missing

Ground reads as flat when the eye gets no parallax between near and far features as the camera moves, and no occlusion between them. Six independent cues supply that. Check each against a measurable threshold.

| # | Cue | Test | Failing value | Target |
|---|---|---|---|---|
| 1 | **Silhouette relief** | vertical range of the ground mesh over a 4 m window | < 0.05 m | 0.15 – 0.6 m over 4 m |
| 2 | **Sub-mesh parallax** | does the texture shift relative to the mesh when the camera translates 0.3 m? | no | POM depth 0.02 – 0.08 m |
| 3 | **Scale variation** | number of independent spatial frequencies in the albedo | 1 (one tiling texture) | ≥ 4 bands (macro / meso / base / micro) |
| 4 | **Contact occlusion** | AO darkening where objects meet the ground | none | 0.3 – 0.7 AO within 0.15 m of contact |
| 5 | **Object relief** | count of ground-resting meshes per m² | 0 | 0.3 – 3 /m² (stones, roots, litter, clumps) |
| 6 | **Material boundaries** | number of distinct ground materials visible in a 10 m view | 1 | 3 – 6, height-blended |

The supplied house ground fails 1, 2, 3, 5 and 6. The supplied road scene passes all six: it has mesh relief, 26 materials, stacked BLEND transition layers, 20,529 triangles of rock decals, and roughly 37 triangles/m² of ground geometry.

A seventh, weaker cue: **wetness pooling in the low places**. Ground that gets uniformly wet in rain is still flat; ground where water finds the ruts is not.

## 4.2 The layer stack

Directly transcribed from the road scene's material taxonomy (chapter 01, section 1.3), generalised:

```
G0  substrate            dirt / rock / sand / clay            OPAQUE, always present
G1  surface type         road, verge, field, forest floor     OPAQUE or BLEND, large areas
G2  transitions          road edge, gravel fringe, shoulder   BLEND, narrow bands
G3  use marks            wheel ruts, footpaths, tank tracks   BLEND
G4  water history        puddles, streaks, mud, drying rings  BLEND
G5  embedded solids      stones, roots, cobbles, debris       mesh or BLEND decal
G6  litter               leaves, needles, twigs, gravel       BLEND cards or decals
G7  growth               grass, moss, weeds                   alpha-tested cards + meshes
G8  transient            snow, ash, blood, scorch, craters    runtime decals
```

Two implementation routes, and both are valid:

| | **Splat route** (one material, N layers, weight map) | **Mesh route** (N overlapping meshes) — what the road GLB does |
|---|---|---|
| Boundaries | from a weight texture, height-blended | from mesh outlines + vertex alpha |
| Draw calls | 1 | 1 per layer |
| Layer count limit | 4–8 per pass (sample budget) | unlimited |
| Authoring | paint weights | model and place |
| Overdraw | none | one screen per overlapping BLEND layer |
| Runtime edits (craters) | easy, write the weight map | hard |
| Best for | large terrain | set-dressed hero areas, roads, bases |

Use **splat for the terrain, mesh for the roads and hero ground**. The road GLB is a hero area; a 4 km² map is not.

## 4.3 Splat blending, exactly

For `K` layers with weights `w_k` from the splat map and heights `h_k` from each layer's height channel:

```glsl
// Height-weighted splat. d = transition depth, 0.03 (sharp) to 0.20 (soft).
float maxH = -1e9;
for (k = 0; k < K; ++k) { hk[k] = h[k] + w[k]; maxH = max(maxH, hk[k]); }
float total = 0.0;
for (k = 0; k < K; ++k) { b[k] = max(hk[k] - (maxH - d), 0.0); total += b[k]; }
for (k = 0; k < K; ++k) b[k] /= total;

// Apply to every channel with the same weights, then restore variance:
vec3 albedo = SUM(A[k] * b[k]);
float s = inversesqrt(SUM(b[k]*b[k]));
albedo = mean + (albedo - mean) * s;          // mean = SUM(A_mean[k] * b[k])
```

Sample cost: `K` taps per map. Four layers × (albedo, normal, ORM+height packed) = 12 taps. That is the reason for the 4-layer practical limit at Medium and below.

**Weight-map compression.** Store `K` weights in one RGBA8 texture with `w_3 = 1 − w_0 − w_1 − w_2`, so 4 layers cost 3 channels and the fourth channel carries something else (wetness, disturbance, or a 5th layer's weight). Splat resolution of **0.5 – 2 m per texel** is enough because the height blend supplies the detail; the road GLB achieves its boundaries from per-vertex alpha, which is coarser still.

### Two tiling scales per layer

A single tiling scale produces a visible period at 3–4 repeats. The standard fix is two scales of the same layer, blended by distance:

```glsl
vec3 near = texture(layerA, uv * (1.0/2.0));     // 2 m tile
vec3 far  = texture(layerA, uv * (1.0/17.0));    // 17 m tile, a non-integer ratio
float t   = saturate((dist - 8.0) / 22.0);       // all near at 8 m, all far at 30 m
vec3 c    = mix(near, far, t) ;                  // then variance-restore as above
```

The ratio must be irrational-ish (2 : 17, not 2 : 16) or the two periods beat together into a larger period. Cost: one extra tap per map, per layer. Enable at Medium and above.

## 4.4 Parallax occlusion mapping

POM shifts the texture lookup along the view ray through a height field. It gives real sub-mesh parallax with no geometry, and it is the single highest-value fix for flat ground.

```glsl
// Tangent-space view direction V_ts, height map H (1 = top of the surface)
// depth = relief depth in metres / the UV-space size of one tile
vec2  maxOffset = V_ts.xy / max(V_ts.z, 0.3) * depth;   // the 0.3 clamp kills grazing swim
int   steps     = int(mix(float(STEPS_MAX), float(STEPS_MIN), abs(V_ts.z)));
float dz        = 1.0 / float(steps);
vec2  duv       = maxOffset * dz;

float z = 1.0, h = textureGrad(H, uv, dx, dy).r;
vec2  p = uv;
while (h < z && steps-- > 0) { z -= dz; p -= duv; h = textureGrad(H, p, dx, dy).r; }

// one secant refinement removes the stair-stepping
float hPrev = textureGrad(H, p + duv, dx, dy).r;
float wgt   = (h - z) / ((h - z) - (hPrev - z - dz));
uv = mix(p, p + duv, wgt);
```

Settings and cost:

| Tier | `STEPS_MIN` | `STEPS_MAX` | `depth` | Fade-out distance | Taps added |
|---|---|---|---|---|---|
| Very Low | — (off) | — | 0 | — | 0 |
| Low | — (offset only, 1 tap) | — | 0.015 m | 8 m | 1 |
| Medium | 6 | 16 | 0.030 m | 18 m | 6–17 |
| High | 10 | 28 | 0.050 m | 35 m | 10–29 |
| Ultra High | 14 | 40 | 0.065 m | 55 m | 14–41 |

Three corrections that separate good POM from bad:

1. **Fade `depth` to zero with distance**, over the last 30 % of the range, or the silhouette mismatch at the horizon pops.
2. **Centre the height field.** Use `h' = h − 0.5` so the relief goes both up and down from the mesh surface. Otherwise objects placed on the ground appear to sink (the ground surface rises up to meet them) or float.
3. **Self-shadowing** is cheap and worth it on High+: march from the hit point toward the light for 4–8 steps and take the maximum penetration as a shadow term. It is what makes POM ruts look deep rather than painted.

```glsl
float pomShadow(sampler2D H, vec2 uv, vec3 L_ts, float depth, int n) {
    float h0 = texture(H, uv).r;
    vec2  d  = L_ts.xy / max(L_ts.z, 0.25) * depth / float(n);
    float sh = 0.0;
    for (int i = 1; i <= n; ++i) {
        float h = texture(H, uv + d * float(i)).r;
        sh = max(sh, (h - h0 - float(i)/float(n)) * (1.0 - float(i)/float(n)));
    }
    return 1.0 - saturate(sh * 4.0);
}
```

POM has no silhouette. Where the camera can see the ground's edge — a rut lip, a road shoulder, a crater rim — it must be real geometry.

## 4.5 Displacement and tessellation

Real relief, and the only thing that fixes cue 1.

```
Budget rule: displacement triangle density should match the screen, not the world.
    target edge length on screen  = 8 - 16 px
    world edge length at distance d, vertical FOV f, screen height Hpx:
        e_world = (8 ... 16) * 2 * d * tan(f/2) / Hpx

At 1080p, f = 55 deg, 12 px target:
    d = 2 m   -> e = 0.023 m   (1900 tris/m^2)
    d = 5 m   -> e = 0.058 m   (300 tris/m^2)
    d = 15 m  -> e = 0.173 m   (33 tris/m^2)
    d = 40 m  -> e = 0.462 m   (4.7 tris/m^2)
```

Apply displacement only inside a radius and only to the low-frequency part of the height field, leaving the high-frequency part to POM and the normal map:

```
h_mesh(p)   = lowpass(h, cutoff = 2 * e_world)      displaced as geometry
h_pom(p)    = h - h_mesh                            left to POM
```

Doing both without the split double-counts the relief and makes the ground look inflated.

Hard constraints:

- **Clamp the displacement so characters' feet do not sink.** Leave out swells above ~0.25 m unless the collision mesh is displaced too.
- **Sample the material at the undisplaced position**, or the texture slides across the moved surface.
- **Never displace beyond what the mesh can carry**: if the tessellation gives 0.06 m edges, a 0.01 m feature produces spikes, not relief. Low-pass the height field to twice the edge length first.

| Tier | Displacement | Radius | Peak tris/m² | Collision |
|---|---|---|---|---|
| Very Low | none | — | 0 | flat |
| Low | none | — | 0 | flat |
| Medium | baked into the mesh at authoring (static) | whole level | 15 | matches |
| High | runtime tessellation | 45 m | 400 | baked, matches Medium |
| Ultra High | runtime tessellation | 90 m | 1200 | baked, matches Medium |

Note the collision row: physics must not change with the graphics tier. Bake one collision surface from the Medium displacement and use it at every tier.

## 4.6 Mesh ground layers, the road-GLB method

For hero ground, model the layers. From the measured road scene:

| Layer | Mesh | Tris | alphaMode | Notes |
|---|---|---|---|---|
| base | `Dirt_Road_Bare`, `Ground_Dirt` | 3,487+ | OPAQUE | continuous |
| surface | `Dirt_Road` | 10,052 | BLEND | the road itself, over the base |
| use | `Dirt_Road_Trails` | 5,292 | BLEND | ruts over the road |
| transition | `Road_Edge_Gravel_Dusty` | 3,126 | BLEND | road to verge |
| water | `Puddle_Streaks` | 3,693 | BLEND | roughness 0.778 |
| solids | `Rock_Decal` | 20,529 | BLEND | embedded stones |
| litter | `Fallen_Maple_Leaves`, `Fallen_Generic_Leaves` | 19,154 | BLEND | |

Construction rules:

```
1  Each layer is a sheet lofted over the base, offset by
       eps = 0.004 + 0.0008 * d_near   metres
   (thicker than a wall decal because the ground is seen at grazing angles).
2  Vertex alpha drives the fade; author alpha = 0 on the boundary ring and
   alpha = 1 two to four vertices inward. Low vertex density is fine - the
   height blend supplies the crisp edge.
3  Sort back-to-front by layer index, not by distance. Layer order is fixed
   and authored; distance sorting will flicker between coplanar sheets.
4  Depth write OFF for BLEND layers, depth test LESS_EQUAL.
5  All layers share one atlas where possible; the road GLB shares the normal
   and ORM of its two grass variants (tex26, tex27) across different albedos.
```

Overdraw budget: a ground view with 5 overlapping BLEND layers costs **5 screens of ground-area fill**. If the ground occupies 45 % of the screen that is 2.25 screens of extra fill. At roughly 0.15 ms per screen of simple shading on a mid-range GPU at 1080p (volume 2, chapter 7.28), that is about 0.34 ms — acceptable, but it is why the Very Low tier must collapse the stack:

| Tier | Mesh ground layers kept |
|---|---|
| Very Low | base only; everything else baked into the base albedo offline |
| Low | base + surface + transition (3) |
| Medium | + use marks + water (5) |
| High | + solids + litter (7) |
| Ultra High | all, plus a second litter pass near the camera |

## 4.7 Scatter: stones, roots, litter, grass

Cue 5. The numbers that matter are densities, and they are small.

| Element | Size | Density | Draw distance | Representation |
|---|---|---|---|---|
| Large stones | 0.2 – 0.6 m | 0.02 – 0.08 /m² | 60 m | mesh, 200–600 tris, 3 LODs |
| Small stones | 0.03 – 0.12 m | 0.5 – 3 /m² | 18 m | mesh, 40–120 tris, 2 LODs |
| Gravel | 0.01 – 0.03 m | — | 6 m | POM + a sparse decal, never meshes |
| Exposed roots | 0.4 – 2 m | 0.01 – 0.05 /m² near trees | 40 m | mesh |
| Twigs / sticks | 0.1 – 0.5 m | 0.3 – 1.5 /m² | 14 m | 2–4 tri cards |
| Leaf litter | 0.03 – 0.1 m | clustered | 20 m | decal sheets, not per-leaf |
| Grass clumps | 0.1 – 0.5 m | 8 – 60 /m² | 25 – 45 m | 6–20 tri cards, instanced |
| Moss clumps | 0.02 – 0.09 m | 400 – 1200 /m² at M=1 | 4 m | chapter 03 |

Placement rules that matter more than density:

```
- Stones sink. Push each 25-45 % of its height below the surface and let the
  ground's contact AO (4.8) cover the intersection. A stone resting exactly on
  a plane reads as a floating prop.
- Stones cluster. Use a two-level process: Poisson-disc cluster centres at
  0.01-0.03 /m^2, then a local density of 2-8 /m^2 inside a 1-3 m radius.
  Uniform scatter looks sprinkled.
- Litter accumulates where wind and water put it: against walls, in ruts, on
  the lee side of obstacles, in corners. Drive it with the same cavity and
  low-height masks as moss, plus a wind-direction term.
- Grass avoids paths, ruts and the drip line under dense canopy, and
  concentrates on verges. Use the ground disturbance channel.
- Nothing scatters on a slope above its angle of repose: ~34 deg for loose
  gravel, ~40 deg for soil with roots.
```

## 4.8 Contact: making objects sit in the ground

Cue 4, and the cheapest of the six.

| Technique | Cost | Effect |
|---|---|---|
| **Contact AO decal** | one small projected decal per object | darkens the ground within 0.1–0.25 m of contact to AO 0.35–0.7 |
| **RVT blend** | 1 virtual-texture sample in the object's shader | the object's base samples the ground's cached colour and blends over 0.05–0.2 m |
| **Dither-blend edge** | 0 | alpha-dither the object's lowest 0.05 m against the ground |
| **Ground-skirt geometry** | a few triangles | a flared sheet of ground material around the object's base |

Runtime Virtual Texture is the production answer for large scenes: the terrain's full layered material is evaluated once into a cached page, and every mesh that touches the ground samples that cache instead of re-running the terrain material.

```
RVT facts to plan with:
- A virtual-texture sample is roughly 2 texture fetches (page indirection + page).
- Pages are re-rendered on demand; a large simultaneous invalidation (a vehicle
  crossing fresh terrain, a crater) causes a frame spike. Budget page updates
  per frame and spread them.
- Meshes marked "render to RVT only" cost nothing per frame in the main pass:
  ground clutter and mesh decals can be baked into the cache and then skipped
  entirely. This is the largest single saving available on low-end hardware.
- Reported cost when misconfigured: ~7 ms in one community case. Profile
  virtual-texturing separately from the rest of the frame.
```

| Tier | Contact technique |
|---|---|
| Very Low | dither-blend edge only |
| Low | contact AO decal |
| Medium | contact AO decal + RVT colour blend |
| High | RVT colour + normal blend, 0.15 m width |
| Ultra High | RVT colour + normal + height blend, 0.25 m width |

## 4.9 Ruts, tracks and deformation

A tank track or a wheel rut is the clearest evidence that the ground is a surface and not a picture.

```
DEFORMATION BUFFER
  A camera-anchored R16 or RG16 render target covering 48-96 m around the player.
  R = depression depth in metres, 0..0.35
  G = disturbance (clears moss, grass, litter), 0..1

  write:  each wheel/track contact stamps a capsule footprint:
              depth = min(depthMax, load_kPa / soilStiffness_kPa * 0.1)
              typical: 0.02-0.06 m for a jeep, 0.10-0.25 m for a tracked vehicle
                       in soft soil; 0.00-0.01 m on hard-packed road
  read:   terrain displacement adds -R; terrain normal is perturbed by grad(R);
          grass and moss density multiply by (1 - G);
          the wetness term adds +R * 2 so water pools in the ruts.
  decay:  R *= exp(-dt / tau), tau = 120-600 s for soil, 20-60 s for snow
          slush, infinite for a persistent map.
  scroll: re-project on camera move, clearing the newly exposed border.
```

Memory: 96 m at 4 cm/texel is 2400² × 4 B = 23 MB for RG16. At 8 cm/texel, 1200² × 4 B = 5.8 MB. Use 8 cm on Medium, 4 cm on High and Ultra High, and disable below.

## 4.10 Water on the ground

```glsl
// Puddle depth from a flow-accumulation / height field W (authored or derived),
// modulated by the weather state w in 0..1 and by the deformation buffer.
float puddle = saturate((W + R * 2.0 - (1.0 - w)) * 6.0);

// Wet ground: porous materials darken and smooth.
albedo    *= mix(1.0, 0.72, w * porosity);           // 0.72 is the measured-ish ratio for soil
roughness  = mix(roughness, 0.30, w * porosity);
// Standing water goes further:
albedo     = mix(albedo, vec3(0.012,0.014,0.013), puddle);
roughness  = mix(roughness, 0.045, puddle);
normal     = normalize(mix(normal, geoNormal, puddle));   // puddles are flat
F0         = mix(F0, vec3(0.02), puddle);
```

The things that make puddles read:

- They are **flat**: force the normal to the geometric normal inside them. Rippled puddles in still air are a common error.
- They have a **meniscus**: a 2–5 cm darkened damp ring outside the water edge.
- They **fill the low places**, so they must be driven by the same height field that drives displacement and the deformation buffer.
- They **dry from the edges inward** over 60–600 s after rain stops.

## 4.11 Fixing the supplied house ground specifically

| Problem measured | Fix | Tier from |
|---|---|---|
| 324 px/m unique painted map | Replace with 4 tiling layers at 512 px/m + splat weights at 1 m/texel | Low |
| Single spatial frequency | Add macro noise (64 m, 11 m) and a detail layer at 16× | Low |
| No height, no parallax | Author height per layer; POM at `depth` 0.03 m | Medium |
| Near-planar mesh | Displace by a low-passed height field; 0.2–0.4 m of relief over the 13 m plot | Medium (baked) / High (runtime) |
| No scatter | 0.05 /m² large stones, 1.5 /m² small, 20 /m² grass clumps, litter in the corners | Low |
| No contact occlusion | Contact AO decals under the house, steps and props | Very Low |
| baseColorFactor 0.409 darkening | Remove; bake the darkening into the layer albedos at their correct values | all |
| No wetness | Add the chapter-wide weather term; puddles against the wall footing | Medium |

## 4.12 Budget summary

| Item | Very Low | Low | Medium | High | Ultra High |
|---|---|---|---|---|---|
| Splat layers | 1 | 2 | 4 | 4 (+2 near) | 6 |
| Tiling scales per layer | 1 | 1 | 2 | 2 | 2 + hex tiling |
| POM | off | offset, 1 tap | 6–16 steps | 10–28 + self-shadow | 14–40 + self-shadow |
| Displacement | none | none | baked | tessellated to 45 m | tessellated to 90 m |
| Mesh ground layers | 1 | 3 | 5 | 7 | 8 |
| RVT | off | off | colour | colour + normal | colour + normal + height |
| Deformation buffer | off | off | 8 cm / 48 m | 4 cm / 64 m | 4 cm / 96 m |
| Scatter draw distance | 8 m | 14 m | 25 m | 40 m | 60 m |
| Grass density | 0 | 6 /m² | 18 /m² | 40 /m² | 60 /m² |
| Puddles | off | wetness only | puddles flat | + meniscus + drying | + ripples under rain |
| Estimated ground GPU cost | 0.25 ms | 0.5 ms | 1.1 ms | 2.2 ms | 3.6 ms |

Cost row is a constructed estimate for a 1080p view with ground at 45 % of the screen on a mid-range GPU. Measure on the target.

## Rules of thumb

- Flat ground is a parallax problem before it is a texture problem. POM at 0.03 m depth buys more than doubling the texture resolution.
- Four layers, two tiling scales each, height-blended, with a 1 m/texel weight map. That is the whole terrain material.
- Displace the low frequencies as geometry and leave the high frequencies to POM; splitting the height field is what stops the ground looking inflated.
- The splat weight map can be coarse. The height blend makes the boundary sharp.
- Everything that rests on the ground must sink into it and darken it. Contact AO is almost free and fixes the most obvious error.
- Scatter clusters; it does not sprinkle. Two-level Poisson placement.
- Water pools in the low places and in the ruts. A uniform wetness value is a wet picture, not wet ground.
- Keep one collision surface across all tiers.

## Sources

- The layer taxonomy and the "road as a BLEND layer over a bare base" structure are read directly from `update_dirt_road_through_forest.glb`: 26 materials, their alpha modes, triangle counts and shared textures (chapter 01, section 1.3).
- The failures listed for the house ground are measured from `fantasy_house_with_interior.glb`: `GROUND_ground_0`, material 3, 2048² base colour, baseColorFactor 0.409, 324 px/m.
- Height-based splat blending as "treat painted layers as height, not alpha": the standard formulation, e.g. the jMonkeyEngine terrain-splatting thread (https://hub.jmonkeyengine.org/t/improved-terrain-splatting-and-parallax-mapping/27844) and Unity community height-blend terrain shaders.
- Parallax occlusion mapping: Brawley and Tatarchuk, ShaderX3 (2005); quadtree displacement with height blending, Drobot (https://www.gamedevs.org/uploads/quadtree-displacement-mapping-with-height-blending.pdf).
- Displacement/parallax splitting, reading the material at the undisplaced point, leaving out broad swells so feet are not swallowed: these constraints match the approach described in a public terrain-relief implementation (https://github.com/VlcekM/MeitouClient/pull/48).
- Runtime Virtual Textures: Epic's documentation (https://dev.epicgames.com/documentation/unreal-engine/runtimevirtual-texturing-quick-start-in-unreal-engine); the "render to RVT only, pay nothing per frame" property and the ~7 ms misconfiguration case are community reports (polycount, ArtStation) and are labelled as such.
- Densities, depths, decay constants, memory figures and all GPU cost figures are constructed planning values. Measure on the target.
