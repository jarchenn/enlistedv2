# Asset manifest, licensing and provenance

Start with **`BATTLE-FOUNDRY-SPEC.md`**. The models and reference photographs are byte-for-byte originals.

## 1. Licensing — read before building anything shippable

Embedded author and licence metadata, read from the GLB JSON.

| Model | Embedded author and source | Licence | Shippable commercially |
|---|---|---|---|
| `fantasy_house_with_interior.glb` | [CGMeller](https://sketchfab.com/3d-models/fantasy-house-with-interior-68f07f44addf4b44b137e0d2d5a9d0e4) | **CC-BY-NC-4.0** | **no** |
| `stone_with_moss.glb` | [Plutonium.software](https://sketchfab.com/3d-models/stone-with-moss-01fce300303645b4aa3086cce3d4250f) | **CC-BY-NC-4.0** | **no** |
| `update_dirt_road_through_forest.glb` | [99.Miles](https://sketchfab.com/3d-models/update-dirt-road-through-forest-c4676cdf7715484382400ff63faffd45) | CC-BY-4.0 | yes, with attribution |
| `old_stone_wall_full_of_moss_-_scan.glb` | [Tijerín Art Studio](https://sketchfab.com/3d-models/old-stone-wall-full-of-moss-scan-1d3752d1ca7c4eb9961710d7c35e48f3) | CC-BY-4.0 | yes, with attribution |
| `bridge.glb` | [Nikolayy](https://sketchfab.com/3d-models/bridge-545a57d9785443bca9b135da2e497bc0) | CC-BY-4.0 | yes, with attribution |
| `cc0__apple_moss_bartramia_pomiformis.glb` | [ffish.asia / floraZia.com](https://sketchfab.com/3d-models/cc0-apple-moss-bartramia-pomiformis-ef418bf372554dc29a8b5c3f50ae3506) | CC0-1.0 | yes |
| `reference-photos/*.jpg` | — | **no verified terms in this bundle** | **no** |

The two NC assets are the two the spec studies most closely. That is intentional: they are **material language to imitate with original geometry**, which is what spec §3 and §4 describe. Confirm licence and attribution requirements before distribution.

Spec §9 phase 0b is the CI gate: no NC-licensed mesh or texture referenced by any shipping map or prefab.

## 2. What is in the bundle

```
BATTLE-FOUNDRY-SPEC.md     the plan of record. Start here.
ASSET-MANIFEST.md          this file.

reference/                 the derivation layer. The spec cites it by chapter.
  README.md                index, conventions, engine mapping, provenance
  01  asset audit — every figure parsed from these GLBs
  02  material layering, grunge and decal meshes
  03  moss, lichen and biological wear
  04  ground that is not flat
  05  combustion light, colour and exposure
  06  muzzle flash and gun blast
  07  explosions, artillery and fireballs
  08  dirt ejecta, debris and craters
  09  recoil dynamics for guns, tanks and carriages
  10  the five quality tiers
  11  formula sheet (A1-H5) and checklists (B1-B12)
  99  Blackwater Thaw castle rebuild spec — prior work, carried for continuity

data/                      the same numbers, loadable
  blackbody.json  yref.json  materials.json  surfaces.json
  weapons.json  munitions.json  guns_recoil.json  tiers.json

models/                    six GLBs, unmodified (bridge.glb renamed from its upload hash)
reference-photos/          three photographs, unmodified
```

## 3. Parsed asset data

Read directly from the GLB JSON and binary chunks. Full tables in spec §2 and `reference/01`.

| Asset | Triangles | Primitives | Materials | Images | Nodes |
|---|---|---|---|---|---|
| `fantasy_house_with_interior.glb` | 537,281 | 11,123 | 5 | 10 | 22,374 |
| `update_dirt_road_through_forest.glb` | 341,109 | 27 | 26 | 70 | 56 |
| `old_stone_wall_full_of_moss_-_scan.glb` | 1,000,000 | 12 | 1 | 1 | 16 |
| `cc0__apple_moss_bartramia_pomiformis.glb` | 1,390,394 | 28 | 2 | 2 | 33 |
| `stone_with_moss.glb` | 372 | 1 | 1 | 3 | 4 |
| `bridge.glb` | 1,932 | 1 | 1 | 2 | — |

Three facts that change how these are imported, and that are easy to miss:

1. **The house's BLEND material is 60.6 % of its triangles, and 98.5 % of that is grass and leaves.** 11,050 of the 11,061 primitives are `Dupli` vegetation cards (320,500 tris, median 40 each). Only 11 are authored decals (4,886 tris), of which the weathering set — `LEAKING_WALL`, `LEAKING_BOTTOM`, `LIANE`, `MOSS`, `ASH` — is **1,624 triangles, 0.30 % of the asset**. The un-instanced cards are the dominant import problem, not the house.
2. **Road materials 0, 11 and 12 bind their normal map to `TEXCOORD_1`.** A blanket UV-strip breaks them. The house's UV1/UV2 are genuine duplicates and are safe to strip.
3. **`old_stone_wall` binds its albedo as `emissiveTexture` with `emissiveFactor [1,1,1]`.** Left alone, the wall glows at full albedo. `cc0__apple_moss` uses `KHR_materials_unlit` and must be de-lit.

## 4. Document history

| Rev | Document | Status |
|---|---|---|
| 1 | `Battle_Foundry_Environment_and_Combat_VFX_Spec.md` | superseded; folded into this spec |
| 2 | `combat-vfx-and-surfaces` corpus | superseded as a standalone; corrected and carried as `reference/` |
| **3** | **`BATTLE-FOUNDRY-SPEC.md` + `reference/` + `data/`** | **current** |

Corrections carried into rev 3 are listed in spec §0. In summary: rev 1's 325,386-triangle decal figure was numerically right but needed the vegetation split; rev 2's 2,408-triangle decal figure was wrong; rev 2's UV-strip rule was unsafe on the road; neither recorded licences; rev 1's recoil timings were 2–3× slow and its flash core was ~4600 K.

## 5. Provenance of every number

The spec tags each quantity **[P]** parsed, **[M]** measured and cited, **[D]** derived here from [P]/[M] inputs, or **[A]** authored tuning seed. **No performance figure in this bundle was measured on Battle Foundry.** No shader code in it was compiled. Replace every [A] with an in-engine measurement before locking a preset, and record which ones changed.

## 6. SHA-256

```text
af4ebea29c1e4a2509910fac6c1f90429d2ecff4a7fb9915a1c8bb6358840a8a  BATTLE-FOUNDRY-SPEC.md
b7fdb2e2e9b795de045d0c79ae99d25e1b1482485b72809af9509fcba0c35580  reference/01-asset-audit-and-what-to-copy.md
2a07dd1a90c663bbad6b6b847dad6fe98e68f150bf17b077cb1f2dce68491eff  reference/02-material-layering-grunge-and-decals.md
701d365678da33335c39da30cd5bc6c3ccfaf58fbfec8862ab2f04a7a6c890e3  reference/03-moss-lichen-and-biological-wear.md
2cb90d967ecbff0cf0f14218b3295f94ed49f6801d110e194870596144160e16  reference/04-ground-that-is-not-flat.md
844453f4ef3af164dcb5ff1bc7cbfd15b2840d9a201807b5fdac149ff867bd4c  reference/05-combustion-light-colour-and-exposure.md
2c8547a348dce445878af75dfc4e86533fc30fe77ef6c28631b4d2a0a74a0980  reference/06-muzzle-flash-and-gun-blast.md
5517f017cb97810a763fae193a0f10dc1aaa87b54d96dbf862c08f0b8eaddfa4  reference/07-explosions-artillery-and-fireballs.md
17aefd17eabd237ccc8ad9b724b859037eb7955d4b476551f11e158fa2a35285  reference/08-dirt-ejecta-debris-and-craters.md
75eff695a14ab2f82e618aa9e4892b943ddd7c51b36581b0cd419f4454cbe6bd  reference/09-recoil-dynamics-for-guns-tanks-and-carriages.md
997e3ecf92335cee7394fa7402019fbe9687b9c289935b6ab60bf92305a04736  reference/10-quality-tiers-very-low-to-ultra-high.md
b7d4a59e9f36e6361b92dc85e9153de286389a58faa854230c3ae8240037a01b  reference/11-formula-sheet-and-checklists.md
5b0903c7d6cef4e90f66fafd19fb946732a4470133d99c9cef12fa4829fee654  reference/99-blackwater-thaw-castle-spec.md
a272e2fc81299084ca309ca73077f4447f3cca1a6f9004ce254c37934d3a581d  reference/README.md
1acceacf6b26adefaad66a40091fbfb7d3f5c88d19e17bca24ba270194fe562f  data/blackbody.json
2448981fa35c095d046de4b1fe76910478fd0b1104d90b25d75e85b9d081fb40  data/guns_recoil.json
86d300f234d1a85ec6037280fd49a45cc4f4cc62c2d9a1875d4f955d6004b85c  data/materials.json
9e8f4c65bd2badb59f952cef3e44c81090574620beb6decb4e262ffdc16baef4  data/munitions.json
9dac4d620281aedb2029491e7c3464c0d136560a32ad6d4db09534025685d4ad  data/surfaces.json
a673cccecc23f9048b77818f8050e01e1b8adf9f42677c724e3c649307006fac  data/tiers.json
6e4359b16de73e3c7fde0b00130a524052a89ba267109e1d1e6eae773937f5c2  data/weapons.json
69eda8d5cd0eccdaffdfd7deb9965805c13398bfb19d680859b96321cb19aa3e  data/yref.json
cead6495f89a0ec3874c970525da729d4ec42314aac9ac32f9245f0ab746930e  models/bridge.glb
ef69f7cf7bf6090e701a64a9cc76e9bdf0edbe9bdffee18e329041216e348ad1  models/cc0__apple_moss_bartramia_pomiformis.glb
c0db20f547ddc032e668ebf22d61b5ca4d8b5fd00b37881eef19e1159ad0cd9e  models/fantasy_house_with_interior.glb
943ca799d49492aea2dd02a85b3fd92b1da4d296f0f8a05f62bd2e7d90725b0a  models/old_stone_wall_full_of_moss_-_scan.glb
0c925a83a2d1431ba014900388f97ec383c1858d304cbeb09f372b428de1caed  models/stone_with_moss.glb
508b50ff8c41671e65822c829971209d0480ebfdcf41e76035f3f4dfdcb954b6  models/update_dirt_road_through_forest.glb
91ff57b72b68be3a53a154841e94af585d1011f25436bd6c2bcc543e61c195b7  reference-photos/Green_Moss_Texture_high_resolution_preview_1741075.jpg
126faad6951ae14c96ac910b9f6828d25f157ac6357ba67ef70907b184db3f94  reference-photos/forestgroundtexture.jpg
153f71f914e3751101e808b31ea667579925b5300d54bbe58c98f2cadf9ba077  reference-photos/wet-moss-covered-house-wall.jpg
```
