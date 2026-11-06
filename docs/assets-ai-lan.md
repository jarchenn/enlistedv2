# Supplied GLBs for the AI / LAN milestone: inventory, canonical choice, licences

Sources:
- `Downloads/Normandy_All_GLB_Models.zip` (25 files)
- `2k_textured_wwii_smoke_grenade.glb`
- `m24_stielhandgranate.glb`
- `mk_2_grenade.glb`
- `us_ranger_infantry_-_call_of_duty_2_hd.glb`

The originals are never modified. They are staged outside the repo at `%TEMP%/nmk/mpai_src`, and only baked derivatives are committed. Author and licence come from each file's embedded `asset.extras` (Sketchfab export). Filenames are not treated as licensing evidence.

## Byte-identical duplicates (SHA-256): one canonical copy each

| Canonical | Duplicates |
|---|---|
| `gangster_tommy_gun.glb` | `gangster_tommy_gun (1).glb`, `gangster_tommy_gun (1)(1).glb` |
| `stg_-_44__sturmgewehr.glb` | `stg_-_44__sturmgewehr(1).glb` |
| `antique_sofa.glb` | `antique_sofa(1).glb` |
| `antique_table.glb` | `antique_table(1).glb` |
| `wardrobe_antique.glb` | `wardrobe_antique(1).glb` |
| `tommy_gun(1).glb` | identical to the earlier `tommy_gun.glb` (not used, see below) |

`antique_dresser.glb` and `antique_dresser (1).glb` are **different** models: 8 versus 20 mesh entries, different hashes.

## Characters (used)

| Source | Use | Author / licence (embedded) | Notes |
|---|---|---|---|
| `ww2_panzergrenadier_2k_cc0.glb` | Axis soldier, `content/characters/axis_grenadier.glb` | britdawgmasterfunk, **CC-BY-4.0** (the filename says CC0, but the file says CC-BY) | Skinned Mixamo-style skeleton (65 joints), 21k verts. No animation clips. |
| `us_ranger_infantry_-_call_of_duty_2_hd.glb` | Allied soldier, variants 0 / 2 / 4 as `allied_ranger_<k>.glb` | Nazar Okruzhko, CC-BY-4.0 | Six static variants in an A-pose, **no skeleton**. Rigged by `tools/bake_characters.py` with the grenadier's skeleton (fitted to height and arm pose), with distance-based weights. The name says it derives from *Call of Duty 2*. Whether the uploader had the right to publish it under CC-BY is **not verified**. The user supplied it for this purpose. |
| `soldier.glb` | not used (fallback) | Danoob, CC-BY-4.0 | 2.6k verts, generic bone names, very small textures. |

**Animation:** none of the sources contains clips. Every soldier state is a **procedural pose** on the supplied skeleton (`src/pose.cpp`):
- stand, walk/run, crouch, prone, aim pitch and twist
- two-bone arm IK onto each weapon's grip sockets, and leg IK with flat feet
- reload (left hand to the magazine and pouch), grenade throw (wind-up, release, follow-through), recoil and death falls

These are not authored or motion-captured animation. Baking real clips (for example from Mixamo) would replace them through the same skeleton names.

## Weapons

| Source | Decision |
|---|---|
| `stg_-_44__sturmgewehr.glb` (ELIZION, CC-BY-4.0) | New weapon **StG 44** (`content/weapons/stg44.json`), Axis sandbox. It is a 1944 assault rifle, so it is flagged `laterWar` and hidden by the strict 6 June preset. The sling node is hidden at bake. |
| `3_grease_gun.glb` (Ruskiy126, CC-BY-4.0) | New weapon **M3 "grease gun"** (`content/weapons/m3_grease_gun.json`), Allied. The M3 / M3A1 variant is decided from the right-side close-up: a crank handle means M3, a finger-hole bolt means M3A1. See the weapon JSON note. It is flagged `laterWar`: M3s reached units in 1944 in small numbers, and the M3A1 came after D-Day. |
| `gangster_tommy_gun.glb` (Pieter Ferreira, CC-BY-4.0) | An **M1928-pattern** Thompson: drum magazine, Cutts compensator, finned barrel, vertical foregrip. **It is not an M1.** The shipped **M1 Thompson keeps its existing M1A1 model and calibration** (weapon identity unchanged). The M1928 is not added as a weapon in this branch: a single fused mesh with no separable drum or bolt. |
| `tommy_gun(1).glb` (lokeig, **CC-BY-NC-4.0**, non-commercial) | Also an M1928 pattern, and non-commercial. Not used (the earlier expansion made the same call). |

## Grenades

| Source | Use |
|---|---|
| `mk_2_grenade.glb` (Nila, CC-BY-4.0) | Allied frag grenade: the E3 item `mk2_grenade` (main session bake). |
| `m24_stielhandgranate.glb` (Özcan Özaltın, CC-BY-4.0) | Axis frag grenade: the E3 item `m24_grenade`. |
| `2k_textured_wwii_smoke_grenade.glb` (KimbellWhatley, CC-BY-4.0) | Smoke grenade item `smoke_grenade` (`content/weapons/smoke_grenade.json`, model `content/weapons/models/smoke`), used by both sides. |

All three are items of the shared grenade core (equipment.cpp): Q / LMB to throw, H quick frag, J quick smoke (in a match).

## Furniture (initial batch: inventoried, integration pending)

Every item below is CC-BY-4.0 with an author in the file. The integration waits for the asset-GLB pipeline that the main session is building for E1 (wire obstacles: `content/assets/models/`), to avoid a second importer:
- antique_table (Luke Arkell; its 3-channel "Scene" animation is not enabled)
- remake antique wooden chair (RaynaudL)
- antique_sofa (Heliona)
- wardrobe_antique (lagesnpiet)
- victorian_bed (BATRIC_18)
- victorian_style_bed (RJDami)
- low_poly_vintage_bedframe (Nico Perry)
- antique_shaving_kit (Jesse Rose)
- antique_spyglass_in_a_box (aqpetteri)
- the_billiards_room (The Hallwyl Museum; an authored scene, so props would be extracted, not inserted whole)
- antique_office_desk (Mkky)
- the two antique_dresser files (Darren McNerney 3D)

The source units vary widely (raw extents from about 0.5 to about 65,000), so every asset needs its own scale correction.
