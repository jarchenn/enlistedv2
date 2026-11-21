# 11. Formula sheet and checklists

Every formula in this volume in one place, with the chapter it comes from. Symbols are defined at first use within each group. Checklists follow at B1–B12.

---

## A. Surfaces, texturing and grunge

**A1 — Texel density** (ch 01)
```
d = sqrt( texels / surface_area_m2 )          texels per linear metre
Targets at 1080p / 60-70 deg FOV:
  hero prop 2048 | interior 512 | exterior wall 256 | ground near 512
  ground mid 128 | terrain far 16 | decal overlay 256
A 2048^2 map at target t covers (2048/t)^2 square metres.
```

**A2 — Legal material ranges** (ch 02)
```
albedo_linear in [0.02, 0.85]      F0_dielectric in [0.02, 0.05]      metallic in {0, 1}
KHR_materials_specular:  F0 = 0.04 * specularFactor * specularColorFactor
```

**A3 — Height blend with variance restoration** (ch 02, ch 04)
```
a = hA + (1-m);  b = hB + m;  top = max(a,b) - d
wA = max(a-top,0); wB = max(b-top,0);  normalise so wA+wB = 1
out = mean + (A*wA + B*wB - mean) / sqrt(wA^2 + wB^2)
    mean = A_mean*wA + B_mean*wB     (the 1x1 mips)
    d = 0.03 (sharp) to 0.20 (soft)
General K-layer form:  sigma' ^2 = sigma^2 * SUM(w_i^2), so divide by sqrt(SUM w_i^2).
```

**A4 — Geometric masks** (ch 02)
```
upMask     = pow( saturate((dot(n,UP) - t)/(1-t)), k )
downMask   = pow( saturate((dot(n,-UP) - t)/(1-t)), k )
shadeMask  = saturate( dot(n, dirShade)*0.5 + 0.5 )
splashMask = pow( saturate(1 - (p.y - groundY)/h), k )        h = 0.25-1.2 m
reachMask  = exp( -((p.y - groundY - 1.05)/0.45)^2 )          hand height
edgeMask   = saturate( ( curv - tE) * kE )
creaseMask = saturate( (-curv - tC) * kC )
cavityMask = saturate( (1 - ao - tA) * kA )
roughen(m) = saturate( (m + (noise-0.5)*spread - threshold)*contrast + 0.5 )
```

**A5 — Normal blending** (ch 02)
```
UDN:      normalize(vec3(n1.xy + n2.xy, n1.z))
Whiteout: normalize(vec3(n1.xy + n2.xy, n1.z * n2.z))
RNM:      t = n1 + (0,0,1);  u = n2 * (-1,-1,1);  normalize(t*dot(t,u) - u*t.z)
```

**A6 — Per-instance variation** (ch 02)
```
YCoCg:  Y = 0.25r + 0.5g + 0.25b ;  Co = 0.5(r-b) ;  Cg = -0.25r + 0.5g - 0.25b
rotate (Co,Cg) by hueAngle, scale by satScale, scale Y by valScale, invert.
Ranges: hueAngle +-0.12 rad, satScale 0.80-1.15, valScale 0.78-1.18,
        roughness +-0.06, plus band index and UV offset from a hash.
```

**A7 — Stochastic (hex) tiling** (ch 02)
```
toHex = [[1, 0], [-0.57735027, 1.15470054]] ;  h = toHex * uv * 3.4641016
three nearest lattice points, barycentric weights w, cubed and renormalised,
sample with per-cell random offsets, then apply A3's variance restoration.
Pass derivatives explicitly (textureGrad).
```

**A8 — Specular anti-aliasing** (ch 02)
```
variance = 0.25 * ( |dFdx(n)|^2 + |dFdy(n)|^2 )
kernel   = min(2*variance, 0.18)
alpha    = roughness^2 ;  roughness' = sqrt( min(1, alpha + kernel) )
```

**A9 — Decal-mesh offset and bias** (ch 02)
```
eps_wall   = max(0.0015, 0.0008 * d_near)        metres
eps_ground = 0.0040 + 0.0008 * d_near
Keep eps/d < 0.0008.  Depth bias: constant -2, slope-scale -1.0 (sign per
depth convention). Depth test LEQUAL, depth write OFF.
```

**A10 — Wetness response** (ch 02, ch 04)
```
albedo    *= mix(1.0, 0.72, w * porosity)
roughness  = mix(roughness, 0.30, w * porosity)
puddle:   albedo -> (0.012,0.014,0.013), roughness -> 0.045,
          normal -> geometric normal, F0 -> 0.02
```

---

## B. Moss and vegetation

**B-M1 — Moss placement field** (ch 03)
```
wetness = 0.40*pow(saturate(1-(p.y-groundY)/1.4), 1.8)
        + 0.25*dripMask + 0.20*cavityMask + 0.15*saturate(1-distToWater/8)
shade   = 0.60*(1 - saturate(sunHours/6)) + 0.40*saturate(dot(n,dirShade)*0.5+0.5)
grip    = 0.50*saturate((dot(n,UP)+0.35)/1.35) + 0.50*substratePorosity
M       = 0.45*wetness + 0.30*shade + 0.25*grip
M      *= (1 - disturbance)
M       = roughen(M, fbm(p*0.7), 0.55, mossThreshold, 5.0)
mossThreshold: 0.35 damp north / 0.55 temperate / 0.75 dry
```

**B-M2 — Moss transmission** (ch 03)
```
wrap = 0.35
diff = saturate((N.L + wrap) / (1+wrap)^2)
back = pow(saturate(dot(V,-L)), 6.0) * 0.55 * thickness
lit  = lightColor * (albedo*diff + transmitColor*back)
transmitColor (linear) = (0.10, 0.21, 0.045)
```

**B-M3 — Shell texturing** (ch 03)
```
h = (shellIndex + 0.5)/shellCount ;  pos += n * h * mossDepth
discard if density(uv) < h ;  occ = mix(0.35, 1.0, h)
mossDepth 0.012-0.025 m ;  shellCount(d) = clamp(round(N*(1 - d/4.0)), 0, N)
```

**B-M4 — Clump scatter** (ch 03)
```
density    = (400..1200) * M  per m^2
axis       = lerp(surfaceNormal, UP, 0.35)
rotation   = smoothNoise(p*0.5) * TAU              (neighbours agree)
scale      = lognormal(median 0.035 m, sigma 0.35), clamp [0.015, 0.09]
sink       = 20-35 % of height
```

---

## C. Ground

**C1 — Six flatness cues and their thresholds** (ch 04)
```
1 silhouette relief   >= 0.15 m over a 4 m window
2 sub-mesh parallax   POM depth >= 0.02 m
3 scale bands         >= 4 (macro 10-100 m, meso 1-10 m, base 5 cm-2 m, micro 1 mm-5 cm)
4 contact occlusion   AO 0.30-0.70 within 0.15 m of contact
5 ground-resting meshes  0.3-3 per m^2
6 distinct materials in a 10 m view  3-6, height-blended
```

**C2 — Parallax occlusion mapping** (ch 04)
```
maxOffset = V_ts.xy / max(V_ts.z, 0.3) * depth
steps     = mix(STEPS_MAX, STEPS_MIN, |V_ts.z|)
linear search then one secant refinement.
Centre the height field: h' = h - 0.5.  Fade depth to 0 over the last 30 %
of the draw range.
Self-shadow: march 4-8 steps toward L, take max penetration.
```

**C3 — Displacement edge length** (ch 04)
```
e_world = px_target * 2 * d * tan(fov_v/2) / H_px          px_target = 8-16
At 1080p, 55 deg, 12 px:  d=2 m -> 0.023 m ; 5 m -> 0.058 ; 15 m -> 0.173 ; 40 m -> 0.462
Split the height field:  h_mesh = lowpass(h, cutoff = 2*e_world) ; h_pom = h - h_mesh
```

**C4 — Two tiling scales** (ch 04)
```
mix(near, far, saturate((dist - 8)/22)) with an irrational-ish ratio (2 : 17),
then apply A3's variance restoration.
```

**C5 — Deformation buffer** (ch 04)
```
R = depth (0..0.35 m), G = disturbance (0..1)
depth = min(depthMax, load_kPa / soilStiffness_kPa * 0.1)
decay  R *= exp(-dt/tau),  tau = 120-600 s soil, 20-60 s slush
wetness += 2*R  (water pools in ruts) ; grass/moss density *= (1-G)
Memory: 96 m at 4 cm = 2400^2 x 4 B = 23 MB ; at 8 cm = 5.8 MB
```

---

## D. Combustion light and colour

**D1 — Blackbody hue, linear Rec.709, max channel 1** (ch 05)
```
1200 K (1.000, 0.095, 0.000)    2500 K (1.000, 0.380, 0.068)
1645 K (1.000, 0.193, 0.000)    3000 K (1.000, 0.484, 0.155)
1800 K (1.000, 0.224, 0.000)    3500 K (1.000, 0.577, 0.261)
2000 K (1.000, 0.266, 0.008)    4000 K (1.000, 0.659, 0.379)
2200 K (1.000, 0.313, 0.028)    5000 K (1.000, 0.795, 0.630)
Wien peak: lambda_max = 2.8977719e6 / T   nm
```

**D2 — Blackbody luminance** (ch 05)
```
1645 K  1.04e4 nits     2500 K  1.79e6     3000 K  9.63e6
1800 K  3.75e4          2800 K  5.28e6     3500 K  3.23e7
2000 K  1.48e5          3200 K  1.63e7     4000 K  8.06e7
2200 K  4.58e5                              5000 K  2.92e8
Multiply by emissivity eps.  Empirical fit over 1600-3000 K:  L_v ∝ T^8.1
```

**D3 — Emissivity from optical depth** (ch 05)
```
eps = 1 - exp(-kappa * density * thickness)     kappa 2.5-6.0 for soot
Typical eps: rifle primary 0.05-0.20 | rifle secondary 0.15-0.40
             tank flash 0.3-0.7 | HE fireball 0.6-0.95 | tracer 0.8-0.95
```

**D4 — Exposure** (ch 05)
```
L_max = 1.2 * 2^EV100          luminance mapping to 1.0 before tone mapping
EV100 = log2( L_avg * 100 / 12.5 )
EV15 -> 39,300 nits | EV12 -> 4,900 | EV9 -> 614 | EV6 -> 77 | EV-2 -> 0.3
stops over clip = log2( eps * L_v / L_max )
```

**D5 — Cooling curve** (ch 05)
```
T(t) = T_amb + (T_0 - T_amb) * (1 + t/tau)^(-n)
rifle primary   T0 1700  tau 0.0004  n 1.20
rifle secondary T0 2500  tau 0.0010  n 1.00
tank flash      T0 2400  tau 0.0060  n 0.90
artillery HE    T0 3200  tau 0.0200  n 0.85
250 kg bomb     T0 3300  tau 0.0600  n 0.80
ember           T0 1350  tau 0.90    n 0.60
```

**D6 — Dynamic light from a glowing sphere** (ch 05)
```
Phi = pi^2 * d^2 * eps * sigma*T^4 * eta_v      lumens   (eta_v from D2's efficacy column)
I   = Phi / (4 pi)                               candela
r_cut = sqrt( I / E_cut )        E_cut = 0.05 lx night, 2 lx day
```

**D7 — Frame-rate-independent flash energy** (ch 06)
```
intensity_this_frame = I_peak * min(1, t_flash / dt)
```

---

## E. Guns

**E1 — Mach disc / intermediate flash position** (ch 06)
```
x_Mach = 0.67 * D * sqrt( p_muzzle / p_ambient )
5.56 mm -> 0.079 m (14 cal)      120 mm -> 2.11 m (18 cal)
Envelope length L = 2.0-3.5 x_Mach (small arms), 4.0-7.0 x_Mach (large calibre)
Max radius R = 0.35-0.55 x_Mach at x = 0.6 x_Mach
Profile  r(x) = R * (x/x0)^0.45 * exp(-1.2*(x/x0 - 1)^2),  x0 = 0.6 x_Mach
Half-angle: 12-18 deg plain | 25-35 deg braked | 8-12 deg flash-hidden
```

**E2 — Muzzle blast equivalent charge** (ch 06)
```
W_eq = m_propellant * (3.9/4.184) * f_blast ,  f_blast = 0.30-0.40
Flash sphere R = 3.77 W_eq^(1/3) ;  ground dust disc radius = 2.5 * that
Disc expansion r_o(t) = R_max (1 - exp(-t/0.055)) ;  r_i = 0.55 r_o
Asymmetry, plain muzzle: forward 1.0, sideways 0.55, rearward 0.25
```

**E3 — Flash temperatures and durations** (ch 06)
```
primary 1645 K (1400-2000) | intermediate 1400-2000 K | secondary 2200-2500 K
small arms visible 1-2 ms (up to 7 ms for .30-06); IR < 30 ms
large calibre < 200 ms MWIR; 120 mm KE saturates thermal for ~600 ms
```

---

## F. Explosions

**F1 — Scaling** (ch 07)
```
Z      = R / W^(1/3)                      scaled distance, m/kg^(1/3)
R_fb   = 3.77 * W^(1/3)                   max fireball radius, m
t_peak = 0.0139 * W^(1/3)                 time to R_fb, s
t_lum  = 0.10 * W^(1/3)                   visible luminous duration, s
R(t)   = 1.03 * (E t^2 / rho)^(1/5)       Taylor-Sedov, E = 4.184e6 W, rho = 1.225
Characteristic times scale as W^(1/3): 10x the charge evolves 2.15x slower.
```

**F2 — Brode overpressure** (ch 07)
```
dp > 10 bar:          dp = 6.7 / Z^3                                    [bar]
0.1 < dp < 10 bar:    dp = 0.975/Z + 1.455/Z^2 + 5.85/Z^3 - 0.019       [bar]
Z=1 -> 826 kPa (brick collapse) | Z=2 -> 156 (lethal) | Z=8 -> 14 (windows break)
```

**F3 — Gurney fragment velocity** (ch 07)
```
V_0 = sqrt(2E) * (M/C + 0.5)^(-0.5)        cylinder
sqrt(2E): TNT 2440, Comp B 2700, RDX 2930 m/s
155 mm M107 -> 996 m/s ; throw > 549 m ; blast lethal radius < 3 % of that
Spray pattern: 75 % within +-25 deg of the plane normal to the shell axis
```

**F4 — Timing curves** (ch 07)
```
u = t / t_lum
flashIntensity(u) = pow(max(0,1-u), 3.2) * exp(-2.1 u)
fireballRadius(u) = R_fb * (1 - exp(-2.6 u / 0.19))
smokeAlpha(u)     = smoothstep(0.35, 1.3, u)
lightIntensity(u) = I_peak * pow(T(u)/T_0, 8.1)
```

**F5 — Six-way lighting** (ch 07)
```
w   = Lts * Lts                                       components sum to 1
lit = w.x*(Lts.x>0 ? A.r : B.r) + w.y*(Lts.y>0 ? A.g : B.g) + w.z*(Lts.z>0 ? A.b : B.b)
A = (right, top, back, alpha) ; B = (left, bottom, front, emissive)
4-channel variant: frontMap = 0.25*(x+y+z+w) ; backMap = 0.6 * frontMap
```

**F6 — Motion-vector frame blending** (ch 07; see vol 1 ch 11.8)
```
uvA = uv_N   - mv(uv_N)   * f       * strength
uvB = uv_N+1 + mv(uv_N+1) * (1 - f) * strength
colour = mix(sample(uvA), sample(uvB), f)
Reduces an 11x11 (121-frame) sheet to 6x6 (32 frames) at equal smoothness.
Do not block-compress the motion-vector texture.
```

**F7 — Smoke column** (ch 07)
```
w_0 = 8-25 m/s scaling as W^(1/6)
z(t) ~ z_0 + C sqrt(t),  C = 6-12 m/s^(1/2)
r(z) = r_0 + 0.25 (z - z_0)
albedo over time: 0.04 -> 0.12 at 2 s -> 0.25 at 5 s -> 0.45 at 15 s
```

**F8 — Oblique impact** (ch 07, ch 08)
```
artillery guns 15-20 deg from horizontal ; mortars 70-80 deg
axis = normalize( mix(surfaceNormal, reflect(v_impact, surfaceNormal), 0.55) )
azimuthal density(phi) = 1 + 0.9 cos(phi - phi_downrange)
crater semi-major/semi-minor = 1/sin(elevation), clamp [1, 3.5]
```

---

## G. Ejecta, debris, craters

**G1 — Particle-size thresholds** (ch 08)
```
Stokes  v_t = rho_p g D^2 / (18 mu)        D <~ 0.1 mm
Newton  v_t = sqrt(4 g rho_p D / (3 rho_a Cd))
Stop    L   = 4 rho_p D / (3 rho_a Cd)
D < 0.2 mm   L < 1.5 m   -> SUSPENDED CLOUD (advect, settle at v_t)
0.2 - 20 mm              -> BALLISTIC WITH DRAG (GPU points)
D > 20 mm                -> BALLISTIC, MESH DEBRIS with collision
```

**G2 — Ejecta speed sampling** (ch 08)
```
M(>v) ∝ v^(-3 mu)   ->   v = v_min * u^(-1/(3 mu)),  u ~ U(0,1], clamp to v_max
mu: dry sand 0.41 | damp soil 0.45 | cohesive soil 0.50 | competent rock 0.55
v_min = sqrt(g * R_crater)
v_max = 2.5 * 20 * W^0.18
```

**G3 — Launch geometry** (ch 08)
```
xi    = sqrt(U(0,1])                      area-weighted launch radius
theta = 52 deg - 22 deg * xi              elevation from the surface
curtain cone emerges from the distribution; do not model it directly
range = v^2 sin(2 theta)/g ;  apex = (v sin theta)^2/(2g) ;  t = 2 v sin theta / g
```

**G4 — Column height** (ch 08)
```
v_col = 20 * W^0.18   m/s        apex = (v_col sin 52)^2 / (2g)
apex / crater_diameter ~ 8   across three orders of magnitude of charge
column life ~ 2.3 * t_to_apex
155 mm: v_col 28 m/s, apex 25 m, life 5.2 s
```

**G5 — Crater** (ch 08)
```
D = 1.6 * W^(1/3) * craterDepthScale      depth = 0.4 D      lip = 0.12 D
ejecta volume = pi (D/2)^2 * depth / 2
craterDepthScale: soft soil 1.3 | cohesive 1.0 | gravel 0.55 | asphalt 0.25
                  concrete 0.10 | rock 0.0 | water 0.0 | airburst 0.0
profile(r) = -depth*(1-(r/R)^2)^1.6  +  lip*exp(-((r-R)/(0.35R))^2)
scorch radius 1.8 R ; ejecta blanket 3.5 R with alpha ~ (R/r)^3 ;
disturbance radius 1.4 R ; fragment scars to 30 R
```

**G6 — Blast interaction** (ch 08)
```
impulse on a body:  J = dp(Z) * A_projected * t_positive / 2
                    t_positive ~ 1.8e-3 * W^(1/3) * Z^0.35   s
blast wind:         v = 18 * (W^(1/3)/R)^1.4 m/s, decaying with tau = 0.3 s
camera shake:       amplitude ∝ sqrt(dp(Z)), 0.3-1.2 s
audio delay:        distance / 343 m/s
dust forward scatter: multiply lit colour by 1 + 1.4*pow(saturate(dot(V,-L)), 4)
```

---

## H. Recoil

**H1 — Momentum** (ch 09)
```
p   = m_p v_p + beta m_c v_p ,  beta = 1.30
V_0 = p / m_r         m_r = recoiling mass (tube + breech + piston)
E_r = 0.5 m_r V_0^2
```

**H2 — Gun recoil displacement (constant-force brake)** (ch 09)
```
a    = V_0^2 / (2L)            t_r  = 2L / V_0            F_R = m_r a
recoil:        x(t)  = V_0 t - 0.5 a t^2
counter-recoil: a_cr = f a, f = 0.025-0.06
                x(t') = L - 0.5 a_cr t'^2 ,  t_cr = sqrt(2L/a_cr) = t_r/sqrt(f)
                buffer the last 0.10 L to a smooth stop
M256 120 mm: L 0.280 m, V0 16.9, a 52 g, t_r 33 ms, t_cr 191 ms, F_R 970 kN
The gun recoils ~38 mm BEFORE the projectile leaves the tube.
```

**H3 — Hull response** (ch 09)
```
dV      = p / M
I_pitch ~ M (0.35 L_hull)^2
omega   = p h / I_pitch            h = trunnion height above CG
theta   = omega / (2 pi f_n)
MBT: 0.8-1.0 deg at 1.2-1.3 Hz, zeta 0.25-0.40
Towed gun: 7-9 deg free, reduced to 2-4 deg about dug-in spades, wheels lift
           30-120 mm; first-shot displacement 0.2-0.8 m, decaying as
           exp(-shotCount/1.5)
```

**H4 — Hand-held recoil (damped spring)** (ch 09)
```
x'' + 2 zeta omega x' + omega^2 x = 0 ,  omega = 2 pi f_n
on fire: v += p / m_effective      (impulses ADD; never reset)
pistol 7.0 Hz / 0.45 | carbine 6.0 / 0.50 | battle rifle 5.0 / 0.50
LMG 4.5 / 0.60 | HMG 3.5 / 0.70 | sniper 4.0 / 0.40
settling time = 3 / (zeta omega)
rise = back * (h_bore_above_pivot / r_grip) * k ,  k = 0.6-1.0
bolt-carrier second impulse: +8-25 ms, opposite sign, 0.15-0.35 amplitude
camera coupling: hip 0.55 | aimed 0.30 | braced 0.18 | prone 0.15, low-passed 8-14 Hz
```

**H5 — Barrel whip** (ch 09)
```
mode(s) = 0.5 s^2 (3 - 2s) + 0.5 s^4            s = 0 breech .. 1 muzzle
offset  = up * A * mode(s) * sin(2 pi f t) * exp(-t/tau)
A 0.010-0.040 m, f 8-15 Hz, tau 0.15-0.40 s
```

---

# Checklists

## B1 — Importing a supplied GLB
```
[ ] Calibrate scale from a known object; apply uniformly.
[ ] Strip a UV set only if no material references it AND it matches UV0.
[ ] Check embedded author/licence metadata; flag any NC asset before shipping.
[ ] Count primitives per material; look for un-instanced duplicate cards.
[ ] Clear emissiveTexture where it equals baseColorTexture.
[ ] Clear KHR_materials_unlit; de-light the albedo if lighting is baked in.
[ ] Raise F0 from any crushed value to 0.02-0.035; raise roughness instead.
[ ] Bake node transforms; discard the hierarchy except for animated parts.
[ ] Merge primitives by (material, alphaMode, 32 m cell). Target < 20 draw calls.
[ ] Keep BLEND groups separate and sorted.
[ ] Generate LOD1 50 %, LOD2 20 %, LOD3 5 % or an impostor.
[ ] Compress: BC7 albedo, BC5 normal, BC4 or packed BC7 for ORM.
[ ] Check texel density against A1; retexture or restrict the camera.
[ ] Decimate scans until tris/m matches the silhouette, not the detail.
```

## B2 — Authoring a building surface
```
[ ] One tiling-material atlas; bands tile in u only, v clamped.
[ ] Low-frequency grunge painted into each band's albedo.
[ ] Hue/value variants of a shared pattern, not new patterns.
[ ] Decal mesh for streaks, waterline, moss patches; 0.3-0.6 % of the tris.
[ ] Macro world-space noise at 64 m and 11 m.
[ ] Detail normal + roughness at 8-16x, faded out by 5-12 m.
[ ] Masks: up, down, shade, splash (0.25-1.2 m), reach (1.05 m +- 0.45 m), curvature.
[ ] Every mask roughened with two octaves of noise.
[ ] Height-blend every layer transition; restore variance.
[ ] Specular anti-aliasing on.
[ ] Wetness term wired to the weather state.
[ ] Per-instance hue, value, band and decal-set selection from a hash.
```

## B3 — Diagnosing flat ground
```
[ ] Measure vertical range over a 4 m window.      target >= 0.15 m
[ ] Translate the camera 0.3 m: does the texture shift against the mesh?
[ ] Count independent spatial frequencies in the albedo. target >= 4
[ ] Check for AO darkening within 0.15 m of every contact.
[ ] Count ground-resting meshes per m^2.           target 0.3-3
[ ] Count distinct materials in a 10 m view.       target 3-6
[ ] Check texel density against A1.
[ ] In rain, does water pool in the low places or wet everything equally?
```

## B4 — Building a moss pass
```
[ ] One placement field (B-M1) shared by every representation.
[ ] Per-material substratePorosity set.
[ ] Disturbance cleared along paths, doorways, ruts, under vehicles.
[ ] Moss fills joints first: drive the blend with the HOST's height.
[ ] Transmission back lobe on at Medium and above.
[ ] Silhouette within 3 m: shells or clumps, not a normal map.
[ ] Lichen driven as the inverse of moss (dry, sunlit, high).
[ ] No wind on moss or lichen.
[ ] Scans decimated to the budget and de-lit.
```

## B5 — Building a muzzle flash
```
[ ] x_Mach computed, not hand-placed.
[ ] Four elements: core mesh, intermediate disc, puff, ground dust disc.
[ ] 3D core mesh, not a billboard.
[ ] Temperature-driven colour; absolute radiance; exposure makes the core white.
[ ] Energy integrated over the frame: min(1, t_flash/dt).
[ ] Flash begins AFTER the gun has already recoiled 38 mm (large calibre).
[ ] Dynamic light intensity from D6; radius from E_cut.
[ ] Muzzle device changes the right components (hider removes the secondary).
[ ] Bore-evacuator puff at 0.4-1.5 s.
[ ] Per-shot roll randomised; scale varied by no more than +-12 %.
[ ] Verified at 30, 60 and 144 fps.
```

## B6 — Building an explosion
```
[ ] R_fb, t_peak, t_lum computed from W.
[ ] Surface-response table entry chosen from the material under the burst.
[ ] Six systems sequenced: flash, fireball, shock, ejecta, smoke, residue.
[ ] Fireball sprites distributed on a 0.35 R_fb sphere, lognormal sizes,
    counter-rotating, hashed flipbook start frames, faded in over 2-4 frames.
[ ] Smoke alpha-blended and lit; lit from inside during the first 60 ms.
[ ] Impact angle applied: oblique for guns, steep for mortars, none for airburst.
[ ] Fragments as motion-blurred streaks with energy-conserving alpha.
[ ] Audio delayed by distance / 343.
[ ] Crater, scorch, ejecta blanket and fragment scars written.
[ ] Barrage staggered 0.2-1.5 s within a salvo.
[ ] Global smoke cap enforced, oldest retired first.
```

## B7 — Building ejecta
```
[ ] Three separate systems by particle size (G1).
[ ] Speed sampled from the power law (G2), not a uniform range.
[ ] Launch radius area-weighted; elevation 52 deg -> 30 deg.
[ ] Dust advected and settled, never given ballistic arcs.
[ ] Clods are meshes with collision and a resting state.
[ ] Dome phase for delay-fuze and buried bursts.
[ ] Forward-scatter term on dust.
[ ] Column apex checked against ~8x crater diameter.
[ ] Persistent debris pooled and capped; merged to static.
```

## B8 — Building recoil
```
[ ] Guns use the parabolic brake model, not a spring.
[ ] t_cr / t_r between 4 and 7.
[ ] Pre-exit travel modelled for large calibre.
[ ] Hull pitch under 1 deg for an MBT; towed guns pivot about the spades.
[ ] Barrel whip at 8-15 Hz.
[ ] Vehicle sheds dust from horizontal surfaces.
[ ] Small arms: impulses accumulate in one spring; climb is emergent.
[ ] Bolt-carrier second impulse present.
[ ] Camera coupled by stance through a low-pass; no double shake through sights.
[ ] Case ejection, suspension travel, crew lag.
```

## B9 — Exposure and light calibration
```
[ ] All emissive authored as absolute radiance (or against a stated reference EV).
[ ] HDR buffers R11G11B10F minimum for emissive particles.
[ ] Histogram metering rejecting the top 4 %.
[ ] EV rate limited: 0.6-1.2 stops/s down, 2.0-4.0 up.
[ ] Adaptation frozen for 150 ms around own-weapon fire.
[ ] EV hard-clamped to the level's authored range.
[ ] Bloom unthresholded, multi-radius, 0.4-2 % energy.
[ ] No effect's apparent brightness changes with frame rate.
```

## B10 — Tier validation
```
[ ] Average and 99th-percentile frame time within the tier budget.
[ ] Average image luminance within +-5 % of Medium.
[ ] Material hue within deltaE 3 of Medium.
[ ] Nothing floats at any tier.
[ ] Craters and collision identical across tiers.
[ ] 12-round barrage held by the governor.
[ ] VRAM within budget with 2 GB of headroom.
[ ] The ten non-negotiables present at Very Low.
```

## B11 — Ordered ladder of cuts
```
 1 internal resolution       8 soft-particle buffer resolution   15 detail layer
 2 Ultra-only terms          9 POM steps / depth / off           16 scatter distance
 3 shadowed effect lights   10 displacement radius / off         17 deformation buffer
 4 volumetric injection     11 second blended layer              18 dust lifetime
 5 shock refraction         12 moss shells -> mat                19 concurrency caps
 6 fragment count           13 decal-mesh overdraw               20 texture resolution
 7 smoke count / lifetime   14 mesh ground layers                   (memory, not time)
Never: contact AO, exposure, aerial perspective, material identity,
       world-memory decals, collision, audio delay.
```

## B12 — Reality check before shipping an effect
```
[ ] Does its size derive from a physical quantity (charge, bore, mass)?
[ ] Does its duration derive from one too?
[ ] Is its colour a temperature, not a swatch?
[ ] Does it interact with the ground it is on?
[ ] Does it leave something behind?
[ ] Does it look different on sand, mud, concrete, snow and water?
[ ] Does the exposure, not the author, decide what clips to white?
[ ] Is it the same at 30, 60 and 144 fps?
[ ] Does it still read at Very Low?
[ ] Does ten of it still fit the budget?
```
