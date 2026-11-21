# 03. Moss, lichen and biological wear

Moss is the cheapest large realism gain available to a damp-temperate scene, and the easiest to get wrong. Wrong moss is uniform green noise lerped over everything by a dot product with up. Right moss is a **thin mat with a silhouette, placed by water, thickest in the joints, and lit with transmission.** This chapter gives the placement field, the three levels of representation, the production pipeline for the three supplied moss GLBs, and the budgets.

## 3.1 Where moss actually grows

Bryophytes have no roots and no cuticle. They take water over their whole surface and lose it as fast. The consequence for placement is simple and strong: **moss is a map of where liquid water persists**, modulated by light being low enough that the surface does not bake.

| Driver | Effect | Implementable proxy |
|---|---|---|
| Persistent moisture | dominant | low height above ground, cavity, below drip lines, near water, north/shaded aspect |
| Low direct sun | strong | accumulated sun-hours, or `dot(n, sunPathMean)` |
| Surface roughness / porosity | strong | per-material constant: stone > brick > weathered timber > plaster > painted metal > glass |
| Slope | moderate | shallow slopes hold mat; vertical holds film; overhangs hold nothing |
| Substrate chemistry | moderate | limestone/lime mortar favours some species; acidic sandstone others. One constant per material is enough. |
| Disturbance | strong | paths, doorways, vehicle tracks, wheel ruts: zero moss |
| Age | scale | mat thickness grows roughly 1–5 mm/yr on stone (*estimate*) |

Observable magnitudes to author against (*field-observation estimates, not measurements*):

| Feature | Typical value |
|---|---|
| Moss mat thickness, stone wall | 5 – 25 mm |
| Moss mat thickness, forest floor | 20 – 80 mm |
| Mat thickness in a mortar joint | 1.5 – 3× the adjacent face |
| Height band on a north wall | ground to 1.0 – 1.6 m, fading upward |
| Height band under a drip line | the whole run, 0.1 – 0.4 m wide |
| Lichen patch diameter, crustose | 20 – 150 mm |
| Algae film thickness | < 0.5 mm (no silhouette, no normal) |
| Clear radius around a used doorway | 0.8 – 1.5 m |

## 3.2 The placement field

One scalar field, evaluated per vertex offline or per pixel at runtime, drives every level of representation. Keeping it single-source is what makes the shader layer, the shell layer and the scattered clumps agree.

```glsl
// All terms in 0..1. Weights sum to 1. p = world position, n = world normal.

float wetness =                                           // where water persists
      0.40 * pow(saturate(1.0 - (p.y - groundY) / H_DAMP), 1.8)   // rising damp, H_DAMP = 1.4 m
    + 0.25 * dripMask(p)                                           // painted or from roof/sill geometry
    + 0.20 * cavityMask                                            // baked AO, short-ray
    + 0.15 * saturate(1.0 - distToWater / 8.0);

float shade =                                             // low direct sun
      0.6 * (1.0 - saturate(sunHours / 6.0))                       // baked, hours/day of direct sun
    + 0.4 * saturate(dot(n, dirShade) * 0.5 + 0.5);                // dirShade = away from the sun's path

float grip  =                                             // can it hold on
      0.5 * saturate((dot(n, UP) + 0.35) / 1.35)                   // up-facing best, vertical ok, overhang poor
    + 0.5 * substratePorosity;                                     // per-material constant, table below

float M = wetness * 0.45 + shade * 0.30 + grip * 0.25;
M *= (1.0 - disturbance);                                 // paths, doors, ruts, tank tracks: 1 = clear
M  = roughen(M, fbm(p * 0.7), 0.55, mossThreshold, 5.0);  // 2.4; two octaves, ~1.4 m and ~0.2 m
```

`substratePorosity`:

| Substrate | Porosity | `mossThreshold` bias |
|---|---|---|
| Lime mortar, soft limestone | 1.00 | −0.10 |
| Granite, concrete | 0.75 | 0.00 |
| Fired brick | 0.70 | 0.00 |
| Weathered timber | 0.65 | +0.02 |
| Asphalt shingle, clay tile | 0.55 | +0.05 |
| Lime plaster | 0.45 | +0.08 |
| Painted timber | 0.25 | +0.20 |
| Bare steel, glass | 0.05 | +0.60 |

`mossThreshold` is the one global knob that makes a whole level wetter or drier. 0.35 is a damp northern village; 0.55 is temperate; 0.75 is dry. Never author moss coverage per asset — author it per level through this constant and the per-material bias, so the whole scene agrees.

**Disturbance** is the term everyone omits, and it is what makes moss look placed rather than applied. Clear it along every path spline, inside every doorway radius, under every parked vehicle, along every wheel rut. A mossy wall with a clean strip where a door swings is instantly believable.

## 3.3 Three levels of representation

| Level | What it is | Silhouette | Use at | Cost |
|---|---|---|---|---|
| **L-A Film** | albedo + roughness shift, no normal, no height | none | > 12 m, and for algae at any distance | 0 extra taps if packed into the blend |
| **L-B Mat** | height-blended material layer with its own normal, parallax, 5–25 mm | implied, not real | 2 – 12 m | 2–3 taps |
| **L-C Fuzz** | shell texturing or scattered clump meshes | real | 0 – 4 m | geometry or 4–8 shells |

The jump that matters visually is **B to C at close range**. A mat with a normal map still reads as paint on a wall when you stand next to it, because moss has a fringe: at a grazing angle you see individual gametophytes break the silhouette.

### L-A and L-B: the material layer

```glsl
// Moss as the top layer of the height-blend stack (chapter 02, section 2.3)
// Height map for moss is near-constant (it is a mat), so the blend is driven by
// the HOST's height: moss fills the joints first.
float hMoss = 0.85 + 0.15 * texture(mossH, uv * mossScale).r;   // nearly flat
float hHost = hostHeight;                                        // mortar joints are low
// swap the usual roles: low host height wins for moss
float m = M;
float a = hHost + (1.0 - m);
float b = hMoss + m;
float top = max(a,b) - 0.045;                                    // d = 0.045, crisp joint edge
...
```

Shading additions that moss needs and stone does not:

```glsl
// 1. Transmission. Moss is thin and backlights strongly. Without this it reads as felt.
//    Wrapped diffuse + a back lobe; no cost beyond a few ALU.
float NdotL  = dot(n, L);
float wrap   = 0.35;                                             // moss: 0.30-0.45
float diff   = saturate((NdotL + wrap) / ((1.0 + wrap) * (1.0 + wrap)));
float back   = pow(saturate(dot(V, -L)), 6.0) * 0.55 * thickness;  // thickness from the mask M
vec3  lit    = lightColor * (albedo * diff + mossTransmitColor * back);
// mossTransmitColor, linear: (0.10, 0.21, 0.045) - yellower and brighter than the albedo

// 2. Roughness. Moss is rough and almost non-specular.
roughness = 0.88;  F0 = 0.035;

// 3. Desaturate and darken with depth into the mat (self-shadowing cue)
albedo *= mix(0.55, 1.0, mossH);
```

### L-C: shell texturing for close moss

Shells are the cheapest real silhouette. Render the surface `N` times, each offset outward along the normal, sampling a density texture whose threshold rises with shell index.

```glsl
// Vertex: offset by shell layer
float h = (float(shellIndex) + 0.5) / float(shellCount);          // 0..1
pos += n * (h * mossDepth);                                       // mossDepth = 0.012-0.025 m

// Fragment: cut away by density
float d = texture(mossDensity, uv * mossTiling).r;                // a noise of blob centres
if (d < h) discard;                                               // or alpha-test
float occ = mix(0.35, 1.0, h);                                    // darker toward the root
vec3  c   = mossAlbedo * occ;
float a   = saturate((d - h) * float(shellCount) * 1.5);          // soften the top shell
```

Costs and settings:

| `shellCount` | Look | Cost | Use |
|---|---|---|---|
| 4 | banded, acceptable past 3 m | 4× fill of the covered area | Low |
| 8 | good at 1–3 m | 8× | Medium |
| 12 | good at 0.5 m | 12× | High |
| 16 | good in contact | 16× | Ultra High |

Shells are fill-rate, not geometry: cost ≈ `shellCount × coveredScreenArea`. Restrict them to a radius around the camera:

```
shellCount(d) = clamp(round(SHELL_MAX * (1 - d / d_shell)), 0, SHELL_MAX)    d_shell = 4.0 m
```

Shells fail at grazing angles (you see between the layers). Fix by fading shell alpha with `saturate(1 - |dot(n,V)|·1.4)` and letting the mat normal map carry the grazing case, or by switching to scattered clumps there.

### L-C alternative: scattered clump meshes

Better silhouette, more memory, no grazing failure. This is what `cc0__apple_moss_bartramia_pomiformis.glb` should become.

```
SCATTER RULES (match Infinigen's boulder-moss rules; see sources)

- Spawn on surface points where M > mossThreshold.
- Density: 400-1200 clumps/m^2 at M = 1, scaled linearly by M.
- Orientation: align clump +Z to lerp(surfaceNormal, UP, 0.35), then rotate about
  that axis by an angle drawn from a smooth noise field sampled at p * 0.5, so
  neighbours lean together instead of looking randomly shuffled.
- Scale: lognormal, median 0.035 m, sigma 0.35, clamped to [0.015, 0.09] m.
- Sink: push each clump 20-35 % of its height into the surface so it does not
  float; the mat layer (L-B) covers the intersection.
- Cull: per-clump, by M and by distance; cross-fade to L-B over 3.5-4.5 m.
```

## 3.4 Production pipeline for the supplied moss assets

### `cc0__apple_moss_bartramia_pomiformis.glb`

As supplied: 1,390,394 triangles for a 124 × 48 × 55 mm clump, 2 images totalling 17.8 MP, both materials `KHR_materials_unlit`.

```
STEP 1  DE-LIGHT
   The albedo contains baked irradiance. Estimate the baked lighting as the
   low-frequency component and divide it out:
       I_baked(x) = gaussian_blur(albedo, sigma = 0.08 * textureWidth)
       albedo_flat = albedo / max(I_baked / mean(I_baked), 0.25)
   Then clamp to the legal albedo range [0.02, 0.85] and check the result
   against the moss values in chapter 02 (0.055, 0.105, 0.030 healthy).
   Clear KHR_materials_unlit.

STEP 2  DECIMATE TO AN LOD CHAIN
   Target budgets for a 35 mm clump used at 0.3-4 m:
       LOD0   1,500 tris    0 - 1.0 m
       LOD1     600 tris    1.0 - 2.0 m
       LOD2     200 tris    2.0 - 4.0 m
       LOD3   2 crossed alpha-tested cards, 8 tris    4 - 10 m
       LOD4   removed; L-B mat takes over
   That is a 927x reduction from LOD0 to source. Use quadric error decimation
   with boundary preservation and UV seams locked.

STEP 3  REBAKE
   Bake LOD0's normal and AO from the 1.39 M source. Texture budget for a
   35 mm clump at 0.3 m viewing distance: 2048 px/m x 0.035 m = 72 px across.
   A 256^2 atlas tile is already generous. Pack 8-16 clump variants into one
   1024^2 albedo + 1024^2 normal + 1024^2 ORM set.

STEP 4  AUTHOR VARIANTS
   From the one scan, produce 6-10 clumps by cutting sub-regions of the mesh,
   scaling non-uniformly (0.8-1.3 in Z), and hue-shifting +-0.08 rad.

RESULT  17.8 MP and 1.39 M tris  ->  3 x 1024^2 (4 MB BC7+BC5+BC4) and
        ~12,000 tris for the whole variant set.
```

### `stone_with_moss.glb`

372 triangles, 3 × 4096², 2.34 × 1.12 × 2.13 m, 1448 px/m. **This asset is already correct** and is the model to imitate: silhouette from a very low-poly mesh, all detail from a high-density texture set.

Two changes: drop the texture set to 2048² (724 px/m, still 1.4× the 512 target) to save 40 MB, and split the moss out of the baked albedo into the L-B layer so that coverage can respond to the placement field instead of being frozen.

### `old_stone_wall_full_of_moss_-_scan.glb`

1,000,000 triangles, 8.77 × 0.84 × 32.91 m, 1 × 4096² bound as **both base colour and emissive**, 230 px/m.

```
STEP 1  Clear the emissive texture binding (it is the albedo; the wall glows).
STEP 2  Cut the 32.9 m run into 4 m modules with matching end profiles so
        the wall can be instanced and extended.
STEP 3  Decimate. A wall seen from 2-20 m needs silhouette, not surface:
            LOD0   12,000 tris per 4 m module   (3,000 tris/m of run)
            LOD1    4,000
            LOD2    1,200
            LOD3      300
        from 121,000 tris/module as supplied: 10x at LOD0.
STEP 4  Rebake normal + AO + curvature from the 1 M source onto LOD0.
        The scan's geometric detail becomes a normal map, which is where it
        belongs: 3,000 tris/m of wall is 10x what the silhouette needs.
STEP 5  Raise texel density. 230 px/m over 317 m^2 is half target. Either
        retexture with a tiling stone material + the scan's normal as a
        unique detail layer, or accept 230 px/m and never let the camera
        closer than 3 m.
STEP 6  Re-derive moss as an L-B layer from the placement field rather than
        the frozen scan albedo, so adjacent modules do not repeat identically.
```

### Summary of the three

| Asset | Source | After pipeline | Reduction |
|---|---|---|---|
| `apple_moss` | 1,390,394 tris, 17.8 MP | ~12,000 tris (10 variants + LODs), 3 × 1024² | 116× tris |
| `stone_with_moss` | 372 tris, 50.3 MP | 372 tris, 3 × 2048² | 4× texture |
| `old_stone_wall` | 1,000,000 tris, 16.8 MP | ~100,000 tris (8 modules × LOD0), same texture | 10× tris |

## 3.5 Moss on the ground

Ground moss is a terrain layer, not a decoration. It belongs in the splat stack of chapter 04, with these specifics:

```
Ground moss layer:
  placement    M from 3.2, with wetness weighted up (0.60) and grip dropped (0.10),
               plus a canopy term: 0.3 * canopyShadow  (moss under trees, not in clearings)
  thickness    0.02 - 0.08 m, driven into the height map so it displaces
  blend d      0.10 (soft, it creeps)
  colour       (0.055, 0.105, 0.030) healthy, (0.085, 0.095, 0.045) dry
  roughness    0.90
  footstep     deform and darken; moss compresses and recovers over 5-20 s
  scatter      L-C clumps at 200-600 /m^2 within 4 m, plus 6-12 cm fern/
               liverwort cards at 8-20 /m^2
```

Moss on the ground must never be a flat green patch. The three things that sell it are (a) it climbs the base of trunks and rocks by 0.1–0.4 m, (b) it stops dead at paths, and (c) it holds standing water, so a wet-moss variant with roughness 0.30 belongs under drip lines.

## 3.6 Lichen and algae

| | Algae film | Crustose lichen | Foliose lichen |
|---|---|---|---|
| Thickness | < 0.5 mm | 0.3 – 2 mm | 1 – 5 mm |
| Representation | L-A only | L-A + weak normal | L-B or small decal cut-outs |
| Placement | wetness, any aspect, very low height band | sun-exposed, dry, old stone, **opposite** to moss | intermediate |
| Albedo (linear) | 0.040, 0.065, 0.030 | 0.28, 0.29, 0.24 (pale grey-green) | 0.20, 0.21, 0.15 |
| Roughness | 0.78 | 0.92 | 0.90 |
| Patch size | continuous film | 20–150 mm discs | 30–120 mm |

The useful trick: lichen and moss are **anti-correlated**. Drive lichen with `L = saturate(shade_inverse × age × (1 − wetness))` from the same field. A wall with moss low and north, lichen high and south, reads as having a history.

The supplied decal atlas already contains a crustose lichen plate and a hanging stringy moss cut-out at v ∈ [0.44, 0.66] — reuse those as the L-C cut-outs rather than authoring new ones.

## 3.7 Wind

Moss does not move. Lichen does not move. **Do not apply vegetation wind to moss** — it is the single clearest tell of procedurally applied foliage. Only the hanging stringy moss and fern cut-outs get motion, and then only the gentle kind:

```
amplitude  0.004 - 0.012 m   (hanging moss), 0.02 - 0.05 m (ferns)
frequency  0.4 - 0.9 Hz
phase      hash(instance) * TAU
mask       vertical gradient: 0 at the attachment point, 1 at the free end, squared
```

## 3.8 Budgets

| Item | Very Low | Low | Medium | High | Ultra High |
|---|---|---|---|---|---|
| Moss representation | L-A film only | L-A + L-B within 8 m | L-B everywhere | L-B + L-C shells within 3 m | L-B + L-C clumps within 4 m |
| Shell count | 0 | 0 | 0 | 8 | 16 |
| Clump scatter density at M = 1 | 0 | 0 | 0 | 400 /m² | 1200 /m² |
| Clump draw distance | — | — | — | 3 m | 4.5 m |
| Transmission (back lobe) | off | off | on | on | on + per-pixel thickness |
| Placement field | per-vertex, baked | per-vertex, baked | per-pixel | per-pixel | per-pixel + cavity from AO |
| Extra taps on a mossy surface | 0 | 2 | 3 | 4 | 6 |
| Moss texture set | shared 512² | shared 1024² | 1024² | 2048² + clump atlas | 2048² + clump atlas + normal |
| Estimated GPU cost in a mossy view | 0.00 ms | 0.08 ms | 0.20 ms | 0.55 ms | 0.90 ms |

Cost figures are constructed estimates for 1080p on a mid-range GPU with moss covering about 30 % of the screen. Measure on the target.

## Rules of thumb

- One placement field, three representations. If the shader layer and the scattered clumps disagree about where moss is, both look wrong.
- Moss is a water map. Height above ground, drip lines and cavities do most of the work; the up-vector dot product does the least.
- Clear moss from paths, doorways and ruts. Absence places moss more convincingly than presence.
- Moss needs transmission. Without a back lobe it reads as green felt.
- Moss fills joints first: drive the height blend with the *host's* height, not the moss's.
- A scan is a source, not an asset. 1.39 M triangles for a 12 cm clump becomes 1,500.
- Never apply wind to moss or lichen.
- Lichen is the inverse of moss: dry, sun-facing, high. Driving both from one field gives a wall a history for free.

## Sources

- Asset figures (`cc0__apple_moss_bartramia_pomiformis.glb`, `stone_with_moss.glb`, `old_stone_wall_full_of_moss_-_scan.glb`) are parsed from the supplied files; see chapter 01.
- World-aligned moss blending driven by the host normal map, with vertex-paint and parameter control, is the standard Unreal practice documented in community breakdowns: https://80.lv/articles/see-how-to-make-custom-moss-shader-burned-fire-decal-in-unreal-engine-5 and https://forums.unrealengine.com/t/blended-moss-material-layer/81640
- Scatter rules (orientation aligned between surface normal and Z, rotation guided by a smooth noise field so neighbours agree, placement on high-Z faces and concave edges) follow Infinigen: Raistrick et al., "Infinite Photorealistic Worlds using Procedural Generation", CVPR 2023, appendix G.4 — https://arxiv.org/pdf/2306.09310
- Runtime Virtual Texture blending of moss and wetness as reusable material functions: https://80.lv/articles/creating-a-mysterious-scene-with-lifelike-foliage-using-speedtree
- Shell texturing is the classic fur technique (Lengyel, Praun, Finkelstein, Hoppe, 2001) applied to moss; shell counts and depths here are authored values.
- Mat thicknesses, height bands, patch sizes and growth rates are field-observation estimates and are labelled as such. No measurement source was consulted for them.
- Transmission values, albedo values and all GPU cost figures are constructed; measure on the target.
