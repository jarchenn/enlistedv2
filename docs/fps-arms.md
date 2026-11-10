# First-person support arm (FA2)

The first-person view shows **one** arm, the support hand. Every gun draws it under the fore-end; knife and grenades draw it on the item. There is no trigger hand. The arm is generated in code (`src/fps_arms.cpp`). It replaced FA1's two-arm rigged model; the model and its rig tool are deleted. The box arms of F4 remain as the fallback if the generated arm cannot load.

## Geometry

The geometry is rebuilt every frame. Its topology is fixed: one dynamic vertex buffer per faction, about 1,250 vertices and 2,080 triangles. Every piece is a swept elliptical tube, and each tube's cross-section keeps a fixed reference axis, so nothing rolls or twists. Every skin tube is closed with rounded caps, so no open end shows its inside.

The hand is real size: the palm 8.5 cm wide and about 9 cm from the wrist crease to the knuckles, the fingers 6-8 cm long and 1.5-1.8 cm thick, about 18 cm from the wrist to the middle fingertip.

| Piece | Shape |
| --- | --- |
| Sleeve | A straight, slightly tapered tube (r 0.044 → 0.053 m) from the wrist off the view. On screen it leaves the heel of the hand toward the lower left (lower right for the mirrored knife / grenade hand), coming a little nearer the eye: about 30 degrees below the horizontal, raised continuously (to about 13 degrees) where that is needed to keep the cuff and 15 cm of sleeve in view. It ends just outside the view, never at the eye. |
| Cuff | A 4 cm band, slightly fuller than the sleeve, ending 2.5 cm up the wrist. |
| Wrist | A short skin tube from inside the cuff to the heel of the hand. The hand turns only here. |
| Palm | From the heel on the near side round the bottom of the held section to the knuckle line under its far-bottom edge. 2.5 cm thick at the heel, 2 cm at the knuckles. |
| Fingers | Four separate fingers, index (toward the muzzle) to little finger, with gaps between them. They run round the section at their own thickness from the knuckle line up the far side, and their tips curl over the top-far edge (stopped 112 degrees round, over the top), so the tips show from the eye. Relaxed, they are shorter and lift off. |
| Thumb | Short (about 5 cm), lying forward along the near side a little below mid height. |

**The held section** is measured on the weapon's mesh: the cross-section at its `gripL` socket, from 6 cm below to 6 cm above it, so a magazine under a magwell is not taken for it. It is treated as an ellipse round the weapon axis:

- the grip point is its bottom, centred;
- the half width is from its lowest 4 cm, where the palm is (clamped to 1.2-4.5 cm), so a bipod or gas cylinder above does not widen it;
- the half height is from its full height (at least the half width, at most 4 cm).

`content/settings/fps_arms.json` → `weapons` → `"<id>"` takes:

- `gripZ`: measure the section that far along the weapon. Low-held guns (Kar 98k, MG 42, MP 40, FG 42, M1C, Panzerschreck) hold further forward, so the hand, the cuff and 15 cm of sleeve stay in view at the hip.
- `grip` [x, y, z] and `section` [half width, half height]: override the measurement (the M2 carry, which has no mesh; the M3, held on the upper magazine housing and the receiver above it; the two flamethrowers, held on the tube just ahead of the rod under it, which the measurement would otherwise take for the section).

**No mesh:** the socket and a 2.2 cm circle.

**Reload:** the hand moves to the feed and relaxes, with the curl dropping to 0.3.

**Swim / mantle:** the hand follows the stroke / ledge target, relaxed.

## Material

- **Sleeve and cuff:** the project's woven wool texture (`content/textures/wool_woven`, ambientCG CC0) is regenerated into a 512 px tile, one turn round the sleeve × 0.28 m of its length.
  - **Colour:** the weave's luminance × the faction colour. Allies are olive drab; Axis is near-black charcoal (linear 0.03), dark enough to read as black while the weave and stitches still show.
  - **Normal map:** the wool's own normal map, plus stitch relief.
  - **Seam:** a stitched seam runs along the sleeve: a darker fold with a row of 8 mm stitches (11 mm pitch, 2 mm thread) 6 mm either side.
  - **Hem:** the cuff's texture adds a stitched hem: two rows round the sleeve, 7 and 16 mm from its edge, and the fold of the turned-up hem.
  - **Stitches:** a clear step lighter than the cloth (`alliesThread` / `axisThread`), rounded across and raised in the normal map so they read at play distance.
  - Matte: roughness 0.9, metallic 0.
- **Hand:** a generated 256 px skin texture shared by every skin piece (`skin` in `fps_arms.json` is its colour), matte (roughness 0.82, metallic 0):
  - a little colour variation;
  - darker where the wrist goes into the cuff, and a wrist crease;
  - darker finger roots and sides between the fingers;
  - creases at the finger joints, as grooves in its normal map.
- **Pass:** the arm draws in the first-person textured pass (`Renderer::viewModelTex`) as three parts: sleeve, cuff and hand.

## Safety and visibility

- **Sight cone:** in ADS the hand drops in 1.2 cm steps until no vertex is within 6 degrees of the sight line.
- **Hidden:** an arm still in the cone, or within 12 cm of the eye near the view centre (25 cm in ADS), is not drawn.
- **Scope:** in the scope view the arm is not drawn (`weapons.cpp`).
- **Setting:** Settings → "Show arms" off hides it.

## For a new weapon

Call `fpsSupportHandDraw(app, cam, weaponToCam, grip, section, curl, rightHand, axis)` (declared in `src/viewmodel.h`) from its viewmodel draw. Give it:

- the weapon-space bottom of the held section (its axis is weapon +z);
- the section's half width and half height;
- whether the sleeve is the Axis one.

A flamethrower passes its tube's support grip and the tube radius twice.

`fpsArmsGrip(weapon, grip, section)` gives the measured grip of a weapon that has a `gripL` socket and a baked mesh.

## Tests

`--selftest-fps` checks:

- every weapon draws one arm (sleeve, cuff, hand) and no trigger hand;
- the sleeve is olive drab for US weapons and charcoal for German ones;
- the hand sits on its grip at the hip;
- at the hip every gun shows the whole hand, the cuff and 15 cm of sleeve on screen (the report lists each gun's hand bounds and the sleeve point in NDC);
- in ADS the arm stays clear of the sight cone and visible for most weapons;
- "Show arms" off and the scoped M1C draw no arm.

`NMK_ARMS_SHOTS=1 --selftest-fps` captures hip / ADS / mid-reload shots of the Garand, Kar 98k, Thompson, MP 40, BAR, MG 42, bazooka, knife and M2 flamethrower (no reload: hip and ADS only; its hand holds the tube at its `leftSupport` socket, no flamethrower-specific code) (`selftest_out/fa2_*`).
