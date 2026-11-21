# 02. Material layering, grunge and decal meshes: reproducing the house

Volume 1 chapter 08 gives the general mathematics of wear masks and height blending. This chapter is the production recipe for the specific look measured in chapter 01: a tiling-material atlas carrying baked low-frequency grunge, overlaid with alpha-blended decal geometry, with per-instance variation applied in the shader. Every constant is a number you can type in.

## 2.1 The layer model

A surface is an ordered stack. Layers are applied bottom-up, each with a mask `m ∈ [0,1]`:

```
L0  substrate        stone / timber / plaster / iron          always present, m = 1
L1  finish           lime wash, paint, tar                    m = f(age, sheltered)
L2  deposition       dust, soot, salt bloom                   m = f(up, sheltered, cavity)
L3  water history    streaks, leach stains, rust runs         m = f(below_feature, vertical, flow)
L4  biology          algae film, moss, lichen                 m = f(damp, shade, cavity, low)
L5  contact wear     polish, chips, scuffs                    m = f(curvature, reach_height, traffic)
L6  transient        wet, snow, ash, blood, scorch            m = f(weather state, events)
```

Order matters physically: moss grows on top of stains, chips cut through all of them to bare substrate, wetness is applied last because it modifies whatever is on top. In the house, L0–L3 are **painted into the atlas albedo** (free, no runtime cost, no variation) and L3–L4 are **decal meshes** (per-instance, costs overdraw). L5 and L6 are not present and are where the asset can be improved.

Decision rule, by scale of the feature and whether it must vary:

| Feature scale | Varies per instance? | Where it goes | Runtime cost |
|---|---|---|---|
| < 5 cm | no | atlas albedo + normal | 0 |
| 5 cm – 2 m | no | atlas albedo (low-frequency mottling) | 0 |
| 5 cm – 2 m | yes | shader mask over a shared grunge texture | 1–2 taps |
| 0.3 – 5 m, follows a specific edge/sill/corner | yes | **decal mesh** | overdraw |
| any, gameplay-driven (scorch, blood, bullet) | yes | projected decal | overdraw + G-buffer write |
| > 10 m | yes | world-space macro noise, per-instance tint | 1 tap on a tiny mip |

## 2.2 Material parameter table (calibrated)

Base colour given as **linear Rec.709**, which is what goes in the texture after sRGB decode. Albedo above 0.85 or below 0.02 is unphysical for a non-emissive surface. F0 is the normal-incidence dielectric Fresnel reflectance.

| Material | Linear albedo (R,G,B) | sRGB hex | Roughness range | F0 | Normal intensity | Notes |
|---|---|---|---|---|---|---|
| Lime plaster, clean | 0.52, 0.49, 0.44 | #C4BFB5 | 0.82 – 0.92 | 0.028 | 0.4 | the cream band |
| Lime plaster, weathered | 0.33, 0.31, 0.28 | #9F9A92 | 0.86 – 0.95 | 0.025 | 0.6 | |
| Oak plank, dry grey | 0.14, 0.13, 0.11 | #6B6760 | 0.72 – 0.88 | 0.030 | 0.8 | the blue-grey band |
| Oak plank, warm | 0.13, 0.085, 0.045 | #684F3C | 0.70 – 0.86 | 0.030 | 0.8 | the tan band |
| Oak plank, dark/wet-aged | 0.055, 0.040, 0.028 | #463A30 | 0.68 – 0.84 | 0.030 | 0.9 | the brown band |
| Granite / limestone, dry | 0.17, 0.17, 0.16 | #737370 | 0.70 – 0.90 | 0.035 | 1.0 | |
| Limestone, lichened | 0.21, 0.22, 0.19 | #80837A | 0.80 – 0.95 | 0.030 | 1.0 | |
| Clay roof tile | 0.095, 0.050, 0.035 | #5B4034 | 0.75 – 0.90 | 0.030 | 0.9 | |
| Slate | 0.030, 0.032, 0.036 | #343639 | 0.45 – 0.70 | 0.035 | 0.7 | |
| Iron, painted, intact | 0.045, 0.045, 0.048 | #3E3E40 | 0.35 – 0.55 | 0.040 | 0.5 | metallic 0 |
| Iron, bare | metal | — | 0.25 – 0.45 | F0 = (0.56,0.57,0.58) | 0.6 | **metallic 1** |
| Rust, mature | 0.085, 0.035, 0.013 | #543322 | 0.80 – 0.98 | 0.035 | 1.0 | **metallic 0** |
| Dry soil | 0.085, 0.060, 0.038 | #534438 | 0.88 – 0.97 | 0.030 | 1.0 | |
| Wet soil | 0.030, 0.021, 0.013 | #332A22 | 0.25 – 0.55 | 0.030 | 0.8 | |
| Moss, healthy | 0.055, 0.105, 0.030 | #3E5630 | 0.78 – 0.92 | 0.035 | 1.0 | add transmission |
| Moss, dry | 0.085, 0.095, 0.045 | #514F36 | 0.85 – 0.95 | 0.035 | 1.0 | |
| Algae film on stone | 0.040, 0.065, 0.030 | #35452C | 0.70 – 0.88 | 0.030 | 0.3 | thin; keeps base normal |
| Soot / scorch | 0.012, 0.012, 0.012 | #1E1E1E | 0.90 – 1.00 | 0.020 | 0.2 | darkest legal albedo |
| Standing water, shallow | 0.012, 0.014, 0.013 | #1F211F | 0.02 – 0.12 | 0.020 | flatten to 0 | |

Three invariants that keep a scene coherent:

```
albedo_linear ∈ [0.02, 0.85]           nothing in nature is outside this
F0_dielectric ∈ [0.02, 0.05]           glass 0.04, water 0.02, most minerals 0.03-0.05
metallic ∈ {0, 1}                       intermediate values only at a rust/metal boundary
```

## 2.3 Height blending, variance-preserved

Linear blending between two materials halves the contrast at the midpoint and reads as airbrushing. The production form, with the variance correction, is:

```glsl
// A, B: layer values (colour, roughness, ao - run the same weights on each)
// hA, hB: layer height maps in 0..1
// m: blend mask 0..1  (0 = all A, 1 = all B)
// d: transition depth. 0.02 = razor sharp, 0.30 = soft. Use 0.05-0.12 for most pairs.
float a = hA + (1.0 - m);
float b = hB + m;
float top = max(a, b) - d;
float wA = max(a - top, 0.0);
float wB = max(b - top, 0.0);
float iw = 1.0 / (wA + wB + 1e-5);
wA *= iw;  wB *= iw;

vec3 blended = A * wA + B * wB;

// Variance restoration. meanA/meanB are the 1x1 mips of A and B (upload as uniforms).
// Without this, every transition band is visibly flatter than either input.
float sigmaScale = inversesqrt(wA*wA + wB*wB);        // = 1 at the ends, 1.414 at 50/50
vec3  mean = A_mean * wA + B_mean * wB;
blended = mean + (blended - mean) * sigmaScale;
```

`sigmaScale` is exact for uncorrelated inputs of equal variance: blending `K` samples with weights `w_i` summing to 1 gives `σ'² = σ² Σ w_i²`, so dividing by `sqrt(Σ w_i²)` restores it.

Height-blend parameters that work:

| Pair | d | Mask source | Note |
|---|---|---|---|
| mortar ← moss | 0.04 | cavity + damp | moss must fill the joint first |
| cobbles ← mud | 0.06 | low + traffic | mud fills between stones |
| plaster ← exposed brick | 0.03 | curvature + noise | hard chipped edge |
| dry soil ← wet soil | 0.15 | height below puddle plane | soft capillary edge |
| stone ← snow | 0.08 | up-facing | |
| paint ← rust | 0.05 | curvature + splash | |
| road ← verge | 0.12 | vertex alpha | the road GLB's transition |

## 2.4 Geometric masks, with constants

All operate on world-space position `p` and world-space normal `n` (use the normal-mapped `n` for ragged edges, the vertex normal for smooth ones).

```glsl
// --- up / down facing -------------------------------------------------------
// t = cut-off cosine, k = sharpness exponent
float upMask   = pow(saturate((dot(n, UP)  - t) / (1.0 - t)), k);
float downMask = pow(saturate((dot(n,-UP)  - t) / (1.0 - t)), k);

// --- shade direction (moss, damp). In the northern hemisphere the sun never
//     passes north, so dirShade = normalize(vec3(0,0,-1)) in a +Z = north frame.
float shadeMask = saturate(dot(n, dirShade) * 0.5 + 0.5);

// --- splash / ground contact ------------------------------------------------
// h = height of the band. Measured on real walls: rain splash 0.25-0.45 m,
// capillary rise in masonry 0.3-0.9 m, road spray 0.5-1.2 m, snow drift 0.1-0.6 m.
float splashMask = pow(saturate(1.0 - (p.y - groundY) / h), kSplash);

// --- curvature (needs a baked curvature map or vertex channel; the fwidth
//     version is faceted on low-poly meshes and should only be a fallback)
float edgeMask   = saturate((curv  - tE) * kE);     // convex: chips, polish
float creaseMask = saturate((-curv - tC) * kC);     // concave: dirt, moss, AO

// --- cavity from baked AO ---------------------------------------------------
float cavityMask = saturate((1.0 - ao - tA) * kA);

// --- ragged-edge operator. Apply to every mask above before using it.
// spread sets how irregular the boundary is; contrast how hard.
float roughen(float m, float noise, float spread, float threshold, float contrast) {
    return saturate((m + (noise - 0.5) * spread - threshold) * contrast + 0.5);
}
```

Constants that produce recognisable results:

| Effect | Mask | t | k | spread | contrast | Band height |
|---|---|---|---|---|---|---|
| Dust on ledges | up | 0.55 | 2.0 | 0.35 | 2.0 | — |
| Snow | up | 0.30 | 1.5 | 0.25 | 3.0 | — |
| Soot under arch | down | 0.40 | 2.5 | 0.40 | 2.0 | — |
| Rain streak below sill | down + flow | 0.20 | 1.0 | 0.60 | 4.0 | 0.6–2.0 m run |
| Splash dirt | splash | — | 1.6 | 0.45 | 3.0 | h = 0.35 m |
| Rising damp | splash | — | 2.2 | 0.30 | 2.5 | h = 0.70 m |
| Moss on wall | shade × splash × cavity | — | 1.0 | 0.55 | 5.0 | h = 1.20 m |
| Algae waterline | splash | — | 6.0 | 0.70 | 8.0 | h = 0.25 m |
| Edge chipping | curvature | 0.35 | — | 0.50 | 30.0 | — |
| Handle polish | curvature × reach | 0.25 | — | 0.20 | 4.0 | 0.8–1.4 m |

**Reach height** is the cheapest "people used this" cue and almost nobody implements it:

```
reachMask = exp(-pow((p.y - groundY - 1.05) / 0.45, 2.0))    // Gaussian centred at 1.05 m, sigma 0.45 m
```

Multiply it into polish, scuff, paint loss and grime around handles, door edges, railings and corners. 1.05 m is roughly hand height for a standing adult; 0.45 m covers crouching and reaching.

## 2.5 Decal meshes: the technique the house is built on

A decal mesh is ordinary geometry, a few millimetres off the host surface, using the shared decal atlas, drawn in the transparent pass. It is the right tool when the overlay must follow a specific edge, a curved surface, or a silhouette that a box projection would smear.

### Construction

```
BUILD A DECAL MESH FROM A HOST SURFACE

input:  host triangles H, a polyline or region R on H, atlas rect (u0,v0,u1,v1)
output: decal mesh D

1  Select the host triangles intersecting R.
2  Duplicate them. Clip the duplicates to R's boundary.
3  Offset every vertex along its smoothed vertex normal by
       eps = max(0.0015, 0.0008 * d_near)         // metres; d_near = nearest camera distance in m
   1.5 mm at 1 m, 4 mm at 5 m, 8 mm at 10 m.
4  Project UVs:
       streak / waterline band  -> u = arclength along the host edge / tileLength
                                   v = (p.y - y0) / bandHeight, clamped into (v0,v1)
       patch cut-out            -> planar projection along the host normal, fitted to the rect
5  Taper alpha at the clip boundary over 3-8 % of the decal's shorter axis
   so the sheet does not end on a hard line.
6  Copy the host's smoothed vertex normal into the decal vertices; do NOT
   recompute, or the decal will shade differently from what it sits on.
```

Measured in the supplied house: the five weathering decal meshes (`LEAKING_WALL`, `LEAKING_BOTTOM`, `LIANE`, `MOSS`, `ASH`) total **1,624 triangles against 537,281**, i.e. **0.30 % of the triangles carry a large share of the realism**. Note that the house shares one BLEND material between those decals and 11,050 vegetation cards (chapter 01, section 1.2); the 60 % of the asset that material accounts for is grass and leaves, not grunge. Split the two on import — they need different draw order, LOD and culling.

### Depth handling

Z-fighting is the failure mode. Three defences, use all three:

```
1  Geometric offset eps as above (the primary defence).
2  Depth bias in the pipeline state:
       constantBias = -2 units, slopeScaleBias = -1.0      (reverse-Z: positive)
3  Depth test LESS_EQUAL, depth write OFF, blend SRC_ALPHA / ONE_MINUS_SRC_ALPHA.
```

With a reverse-Z 32-bit float depth buffer and `near = 0.05 m`, depth precision at `d` metres is roughly `near/d × 2^-23 × d² = d × near × 1.19e-7` metres, i.e. 0.3 µm at 50 m. Geometric offset of 1.5 mm is four orders of magnitude above that; the practical limit is not precision but the decal lifting visibly off a surface seen at a grazing angle. Keep `eps / d < 0.0008`.

### Lighting

Decal meshes must take the host's normal, not their own. If the decal has a normal map (streaks usually should not; moss should):

```glsl
// RNM reorientation of the decal normal onto the host normal
vec3 t = hostN_ts + vec3(0.0, 0.0, 1.0);
vec3 u = decalN_ts * vec3(-1.0, -1.0, 1.0);
vec3 n = normalize(t * dot(t, u) - u * t.z);
```

Fade the decal's normal contribution by its alpha: scale `xy` by `alpha`, then renormalise. Do not lerp toward `(0,0,1)` after normalising — that flattens the host too.

### Cost

A decal mesh costs one transparent draw call per material-group and `A_screen` pixels of overdraw. Budget:

| Tier | Decal-mesh screens of overdraw | Decal draw calls per building | Atlas |
|---|---|---|---|
| Very Low | 0.0 (disabled; grunge from atlas only) | 0 | — |
| Low | 0.15 | 1 | 1024² BC7 |
| Medium | 0.35 | 1 | 2048² BC7 |
| High | 0.7 | 2 (opaque-masked + blended) | 4096² BC7 |
| Ultra High | 1.2 | 3 | 4096² BC7 + 2048² normal |

A cheaper variant for the two tiling bands (streaks, waterline): author them as **alpha-tested** (`alphaCutoff ≈ 0.4`) rather than blended, render them in the opaque pass with depth write on, and they cost no sorting and no blend. Only the soft lower fringe of the waterline needs true blending; split the band into an opaque core and a blended fringe.

### Projected decals, for comparison

| | Decal mesh | Projected (deferred) decal |
|---|---|---|
| Follows curved/broken host | yes | approximately, within the projector box |
| Needs authored geometry | yes | no |
| Cost | overdraw + a transparent draw call | overdraw + G-buffer read-modify-write |
| Works on alpha-tested foliage | yes | no (depth is unreliable) |
| Can be spawned at runtime | awkward | trivially |
| Right for | authored grunge, moss, streaks, posters | bullet holes, scorch, blood, tyre tracks |

Use decal meshes for everything authored, projected decals for everything the game spawns. Chapter 08 budgets runtime scorch decals.

## 2.6 Per-instance variation without extra textures

Three identical houses side by side is the tell. Variation comes from per-instance constants, not per-instance textures.

### Hue and value shift

Work in a luma/chroma basis so value and hue are separable and the operation is three multiplies:

```glsl
// YCoCg-R rotation, exact and cheap
vec3 rgb2ycocg(vec3 c){ return vec3(0.25*c.r+0.5*c.g+0.25*c.b, 0.5*(c.r-c.b), -0.25*c.r+0.5*c.g-0.25*c.b); }
vec3 ycocg2rgb(vec3 y){ return vec3(y.x+y.y-y.z, y.x+y.z, y.x-y.y-y.z); }

vec3 varyInstance(vec3 albedo, float hueAngle, float satScale, float valScale) {
    vec3 y = rgb2ycocg(albedo);
    float s = sin(hueAngle), c = cos(hueAngle);
    vec2 co = vec2(y.y * c - y.z * s, y.y * s + y.z * c) * satScale;
    return max(ycocg2rgb(vec3(y.x * valScale, co.x, co.y)), vec3(0.0));
}
```

Per-instance ranges that read as "different buildings, same village":

| Parameter | Range | Driven by |
|---|---|---|
| `hueAngle` | ±0.12 rad (±7°) | hash(instanceID) |
| `satScale` | 0.80 – 1.15 | hash |
| `valScale` | 0.78 – 1.18 | hash |
| roughness offset | ±0.06 | hash |
| atlas band index | pick 1 of N wood bands | hash |
| decal set index | pick 1 of M decal variants | hash |
| UV offset within band | ±0.5 in u | hash |

A good hash for an instance index or a world-space cell:

```glsl
float hash1(uvec2 x){
    x = x * 1664525u + 1013904223u;
    x.x += x.y * 1664525u;  x.y += x.x * 1664525u;
    x ^= x >> 16u;
    x.x += x.y * 1664525u;
    return float(x.x) * (1.0/4294967296.0);
}
```

### Macro variation across the world

One low-resolution world-space noise, sampled at a tiny mip, breaks the "same material everywhere" read for one texture tap:

```glsl
// 64 m and 11 m octaves, world-aligned, applied to albedo value and roughness
float m1 = texture(macroNoise, p.xz * (1.0/64.0)).r;
float m2 = texture(macroNoise, p.xz * (1.0/11.0)).r;
float macro = m1 * 0.65 + m2 * 0.35;
albedo    *= mix(0.80, 1.20, macro);
roughness  = clamp(roughness + (macro - 0.5) * 0.14, 0.03, 1.0);
```

## 2.7 Breaking tiling

A tiling material repeats at its tile size; the eye finds the period at 3–4 repeats. Three fixes, in increasing cost:

```
1  Macro modulation (section 2.6)         +1 tap   removes value banding, not pattern
2  UV rotation by a per-cell hash         +0 taps  cheap, works on isotropic materials only
3  Stochastic / hex tiling                +2 taps  removes the period entirely
```

Stochastic (hex-grid) tiling samples the texture three times with per-cell random offsets and blends by barycentric weights, with the variance correction from 2.3 applied or the result goes grey:

```glsl
// Heitz & Neyret style, reduced. Returns a non-repeating sample of a tiling texture.
vec3 hexTile(sampler2D tex, vec2 uv, vec3 mean) {
    const mat2 toHex = mat2(1.0, 0.0, -0.57735027, 1.15470054);
    vec2 h = toHex * uv * 3.4641016;              // 2*sqrt(3)
    vec2 hi = floor(h); vec2 f = fract(h);
    float sum = f.x + f.y;
    vec3 v0, v1, v2; vec3 w;
    // three nearest lattice points and their barycentric weights
    vec2 c0, c1, c2;
    if (sum < 1.0) { c0 = hi; c1 = hi + vec2(1,0); c2 = hi + vec2(0,1); w = vec3(1.0-sum, f.x, f.y); }
    else           { c0 = hi + vec2(1,1); c1 = hi + vec2(1,0); c2 = hi + vec2(0,1); w = vec3(sum-1.0, 1.0-f.y, 1.0-f.x); }
    v0 = texture(tex, uv + vec2(hash1(uvec2(c0+64.0)), hash1(uvec2(c0.yx+32.0)))).rgb;
    v1 = texture(tex, uv + vec2(hash1(uvec2(c1+64.0)), hash1(uvec2(c1.yx+32.0)))).rgb;
    v2 = texture(tex, uv + vec2(hash1(uvec2(c2+64.0)), hash1(uvec2(c2.yx+32.0)))).rgb;
    w = w * w * w;  w /= (w.x + w.y + w.z);        // cubic weights: narrower transition
    vec3 s = v0*w.x + v1*w.y + v2*w.z;
    return mean + (s - mean) * inversesqrt(w.x*w.x + w.y*w.y + w.z*w.z);
}
```

Only apply it to the base colour and the height; derive roughness and normal from the already-blended height where possible, or accept two more taps. Gradients must be passed explicitly (`textureGrad`) because the per-cell offsets break the implicit derivative.

Hard rule: hex tiling on **Medium and above only**, and only on ground and large wall surfaces. It triples the tap count of whatever it wraps.

## 2.8 Detail layers: the micro scale

Variation must exist at every scale (volume 1, chapter 08 section 4). The band a tiling texture cannot cover from below is 1 mm – 5 cm.

```glsl
// Detail tiled 8-20x denser than the base, faded out with distance so it
// never aliases. detailFade reaches 0 at d_fade.
float detailFade = saturate(1.0 - dist / d_fade);
vec3  dN = texture(detailNormal, uv * detailScale).xyz * 2.0 - 1.0;
dN.xy *= detailStrength * detailFade;
n = rnm(baseN, normalize(dN));
roughness += (texture(detailRough, uv * detailScale).r - 0.5) * 0.18 * detailFade;
```

| Surface | `detailScale` (× base) | `detailStrength` | `d_fade` (m) |
|---|---|---|---|
| Plaster | 12 | 0.5 | 6 |
| Timber | 8 | 0.7 | 8 |
| Stone | 10 | 0.8 | 10 |
| Soil / road | 16 | 0.9 | 12 |
| Metal | 14 | 0.35 | 5 |

A single shared 512² detail normal + 512² detail roughness (BC5 + BC4, 0.5 MB with mips) serves the whole project.

## 2.9 Roughness anti-aliasing

Without it, every high-frequency normal map shimmers under a moving light and no amount of grunge will read as a real surface. Convert normal-map variance into extra roughness:

```glsl
// Kaplanyan / Tokuyoshi specular AA, per-pixel
vec3  dndx = dFdx(n), dndy = dFdy(n);
float variance = SIGMA2 * (dot(dndx, dndx) + dot(dndy, dndy));   // SIGMA2 = 0.25
float kernelRoughness = min(2.0 * variance, KAPPA);              // KAPPA = 0.18
float alpha = roughness * roughness;
roughness = sqrt(min(1.0, alpha + kernelRoughness));
```

This is not optional on any surface carrying a detail normal. Cost: ~10 ALU.

## 2.10 Sample budget

Taps per pixel for the opaque surface shader, per tier. "Tap" means one filtered texture fetch.

| Component | Very Low | Low | Medium | High | Ultra High |
|---|---|---|---|---|---|
| Base colour | 1 | 1 | 1 | 1 (or 3 hex) | 3 hex |
| Normal | — | 1 | 1 | 1 | 1 |
| ORM (occlusion/rough/metal packed) | — | 1 | 1 | 1 | 1 |
| Detail normal + rough | — | — | 1 (packed) | 2 | 2 |
| Macro noise | — | 1 | 1 | 1 | 1 |
| Second blended layer | — | — | — | 3 | 3 |
| Grunge mask | — | — | 1 | 1 | 2 |
| **Total** | **1** | **4** | **6** | **10** | **13** |
| Decal-mesh overdraw (screens) | 0 | 0.15 | 0.35 | 0.7 | 1.2 |

Rule: the Very Low tier must still look like the same material, just without variation. Keep base colour and the macro tint; drop detail, blending and decals. A tier that changes the *colour* of the world is a broken tier.

## 2.11 Putting it together: the house shader

```
HOUSE SURFACE SHADER (one permutation, branch on tier constants)

in:  uv0 (atlas), p_world, n_world, tangent frame, instanceSeed, curvature (vertex), ao (vertex)

1   bandUV = atlasRect.xy + fract(uv0 * atlasRect.zw * tileCount)       // clamp v, wrap u
2   base   = TIER>=HIGH ? hexTile(albedoAtlas, bandUV, bandMean) : texture(albedoAtlas, bandUV)
3   n_ts   = unpack(texture(normalAtlas, bandUV))
4   orm    = texture(ormAtlas, bandUV)
5   base   = varyInstance(base, hueAngle, satScale, valScale)           // 2.6
6   macro  = sample two world-space octaves; modulate base and roughness  // 2.6
7   detail = tap detail normal + roughness, faded by distance             // 2.8
8   masks  = up/down/shade/splash/reach/curvature, roughened by noise     // 2.4
9   layers = height-blend deposition and biology layers by those masks    // 2.3
10  wet    = apply weather state: albedo *= mix(1, 0.72, w);
                                   roughness = mix(roughness, 0.08, w*porosity)
11  raa    = specular anti-aliasing                                       // 2.9
12  F0     = 0.03 for dielectrics; metallic from orm.b
```

Step 10 is the single cheapest large win available to the supplied house, which has no wetness at all. Wetting a porous surface darkens it by a factor of about 0.65–0.80 in albedo and drops roughness toward 0.05–0.25; the ratio of those two changes is what makes rain read.

## Rules of thumb

- Decide per feature: does it vary per instance? No → paint it into the atlas. Yes → decal mesh or shader mask.
- Height-blend everything, and always restore variance; a soft cross-fade is the signature of an amateur blend.
- Masks must be noisy. A clean geometric mask is as unreal as no mask.
- Reach height and splash height are the two cheapest "humans live here" signals.
- Specular anti-aliasing is mandatory once detail normals exist.
- F0 near 0.03 and roughness near 0.9 for aged dielectrics. Do not crush F0 to hide a gloss problem.
- The weathering decal set is 0.3 % of a building's triangles and a large fraction of its realism. Do not let vegetation cards share its material.
- Per-instance hue, value and band selection cost zero texture memory and remove the clone look.

## Sources

- Material parameter table: albedo and F0 ranges follow the standard dielectric and metal tables in volume 1 chapter 03; roughness ranges are authored values, not measurements.
- Height blending and the variance correction: volume 1 chapter 08 sections 3.2 and 3.4.
- Reoriented normal mapping: Barré-Brisebois and Hill, "Blending in Detail" (2012).
- Stochastic/hex tiling: Heitz and Neyret, "High-Performance By-Example Noise using a Histogram-Preserving Blending Operator", HPG 2018. The reduced form here omits the histogram transform and substitutes the variance rescale; it is cheaper and slightly less exact.
- Specular anti-aliasing: Kaplanyan et al., "Filtering Distributions of Normals for Shading Antialiasing", HPG 2016; Tokuyoshi and Kaplanyan (2019) for the improved kernel. Constants SIGMA2 = 0.25 and KAPPA = 0.18 are the commonly used defaults.
- Decal-mesh offsets, bias values and overdraw budgets are constructed planning figures, not measurements. Measure on the target.
- Atlas layout, UV ranges, triangle counts and material parameters quoted for "the house" are parsed from `fantasy_house_with_interior.glb` (chapter 01).
