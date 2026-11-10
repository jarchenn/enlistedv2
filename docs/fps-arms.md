# First-person support arm (FA2)

The first-person view shows **one** arm, the support hand. Every gun draws it under the fore-end; knife and grenades draw it on the item. There is no trigger hand. The arm is generated in code (`src/fps_arms.cpp`). It replaced FA1's two-arm rigged model; the model and its rig tool are deleted. The box arms of F4 remain as the fallback if the generated arm cannot load.

## Geometry

The geometry is rebuilt every frame. Its topology is fixed: one dynamic vertex buffer per faction, about 840 vertices and 1,340 triangles. Every piece is a swept elliptical tube, and each tube's cross-section keeps a fixed reference axis, so nothing rolls or twists.

| Piece | Shape |
| --- | --- |
| Sleeve | A straight, slightly tapered tube (r 0.044 → 0.053 m) from the wrist back off the bottom-left of the view. Its direction is the wrist's back-down-out direction, blended toward the lower left of the screen (lower right for the mirrored knife / grenade hand). |
| Cuff | A 4 cm band, slightly fuller than the sleeve, ending 3.5 cm up the wrist. |
| Wrist | A short skin tube from inside the cuff to the heel of the hand. The hand turns only here. |
| Palm | A pad round the near-lower side of the held section. The heel is lifted off it, and the thumb side is the fuller part. |
| Fingers | Four, index (toward the muzzle) to little finger. They curl under and up the far side of the section, about 130 degrees at full curl. |
| Thumb | Runs forward along the near side. |

**The held section** is measured on the weapon's mesh as in FA1: the cross-section at its `gripL` socket, within 6 cm below it, so a magazine under a magwell is not taken for it. Its bottom and centre give the grip point, and half its width the radius (clamped to 1.2-4.5 cm).

- **Override:** `content/settings/fps_arms.json` → `weapons` → `{"<id>": {"grip": [x, y, z], "radius": r}}`.
- **No mesh:** the socket and a 2.2 cm radius.

**Reload:** the hand moves to the feed and relaxes, with the curl dropping to 0.3.

**Swim / mantle:** the hand follows the stroke / ledge target, relaxed.

## Material

- **Sleeve and cuff:** the project's woven wool texture (`content/textures/wool_woven`, ambientCG CC0) is regenerated into a 512 px tile, one turn round the sleeve × 0.28 m of its length.
  - **Colour:** the weave's luminance × the faction colour. Allies are olive drab; Axis is near-black charcoal (linear 0.03), dark enough to read as black while the weave and stitches still show.
  - **Normal map:** the wool's own normal map, plus stitch relief.
  - **Seam:** a stitched seam runs along the sleeve: a darker fold with a row of 3 mm stitches 4 mm either side, each a 5 mm pitch in a lighter thread, slightly raised.
  - **Hem:** the cuff's texture adds a stitched hem: two rows round the sleeve, 6 and 13 mm from its edge, and the fold of the turned-up hem.
  - Matte: roughness 0.9, metallic 0.
- **Hand:** skin colour (`skin` in `fps_arms.json`), matte (roughness 0.82, metallic 0), no texture.
- **Pass:** the arm draws in the first-person textured pass (`Renderer::viewModelTex`) as three parts: sleeve, cuff and hand.

## Safety and visibility

- **Sight cone:** in ADS the hand drops in 1.2 cm steps until no vertex is within 6 degrees of the sight line.
- **Hidden:** an arm still in the cone, or within 12 cm of the eye near the view centre (25 cm in ADS), is not drawn.
- **Scope:** in the scope view the arm is not drawn (`weapons.cpp`).
- **Setting:** Settings → "Show arms" off hides it.

## For a new weapon

Call `fpsSupportHandDraw(app, cam, weaponToCam, grip, radius, curl, rightHand, axis)` (declared in `src/viewmodel.h`) from its viewmodel draw. Give it the weapon-space bottom of the held section (its axis is weapon +z), the section radius, and whether the sleeve is the Axis one. A flamethrower passes its tube's support grip and the tube radius.

`fpsArmsGrip(weapon, grip, radius)` gives the measured grip of a weapon that has a `gripL` socket and a baked mesh.

## Tests

`--selftest-fps` checks:

- every weapon draws one arm (sleeve, cuff, hand) and no trigger hand;
- the sleeve is olive drab for US weapons and charcoal for German ones;
- the hand sits on its grip at the hip;
- in ADS the arm stays clear of the sight cone and visible for most weapons;
- "Show arms" off and the scoped M1C draw no arm.

`NMK_ARMS_SHOTS=1 --selftest-fps` captures hip / ADS / mid-reload shots of the Garand, Kar 98k, Thompson, MP 40, BAR, MG 42, bazooka and knife (`selftest_out/fa2_*`).
