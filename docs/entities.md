# New entities (complete update, part 4)

Workstreams E1–E5 of `docs/update_checklist.md`. Tests: `--selftest-entities` (`src/selftest_entities.cpp`).

## Supplied models on map assets (shared pipeline)

Map assets can carry a `"model"` block, the same import spec the weapons use. `NormandyMapmaker.exe --import-weapon-models assets_src/models content` bakes it into `content/assets/models/<dir>/`. The source GLBs stay outside git, in `assets_src/models/` (git-ignored).

| Spec field | Meaning |
|---|---|
| `pivot: "ground"` | Centred in x/z with the lowest point at y = 0, so the model stands on its placement point (weapons keep `"weapon"`: butt at z = 0) |
| `scale` | Absolute scale (the sources are in metres) instead of `realLength` |
| `hide` | Source groups left out, so one model can be cut out of a set |
| `meshEdits` | Bake-time edits of the supplied mesh: re-pose, hide or open islands (see `fps-weapons.md#mesh-edits`) |
| `simplify` | Fraction of each part's triangles kept, by quadric-error simplification (`src/simplify.cpp`). UV seams survive; collapsed corners take the surviving vertex's attributes |
| `lod1` | Also bakes `<dir>/lod1` at `simplify × lod1`, with 256 px textures |
| `lodDist`, `cullDist` | Metres: the distance where the LOD takes over, and where drawing stops |
| `sections: {axis, count}` | Splits every part into `count` slabs along `axis`, so a blast can clear one stretch |
| `thinLinesPx` | Keeps sub-pixel strands visible. The model vertex shader moves vertices out along their normals by that many pixels' worth of angle at their distance, capped at 3 cm. A 2–3 mm wire therefore stays about a pixel wide at range instead of breaking up |

**Other bake behaviour:**
- Only materials used by the kept parts get textures.
- Weapons and assets that share a model folder must carry identical model blocks (checked, warned).

**Runtime:**
- Instances of a model asset draw its parts through the imported-model pass (`R.worldModels`), including shadows.
- The thumbnail and the Creative placement preview show the real model. The preview also tints the collider boxes green or red for valid or invalid placement.
- Collision always comes from the asset's own box parts: models are never used as collision meshes.

**Collider flags:**
- `"k": 2` marks a **player-only** collider on a third Jolt layer (`PLAYER_ONLY`).
  - Only the player capsule meets it.
  - Rays and projectiles, debris, grenades and other simulated bodies all ignore it.
- `"sec": n` ties a collider to destruction section n.

## E1 Wire obstacles

**Source.** `wire_obstacle_defence_set (1).glb` was chosen. Both supplied files have identical geometry: 41 nodes, 33 meshes, 3,215,904 triangles. The "(1)" file has the higher-resolution textures (3.7 MB base colour against 1.1 MB). Rendering the set shows **four complete assemblies**, not five:

| Source groups | Assembly | Used as |
|---|---|---|
| Helix045 | concertina coil, extended: 3.9 m long, 0.86 m high | `wire_concertina` |
| Capsule004 + Capsule003 | post-and-barbed-wire fence (the two groups are its two runs) | `wire_fence` |
| Box003 | knife rest: timber X-trestles and ridge beam with a coil inside | `wire_knife_rest` |
| Helix044 | the **same coil mesh** as Helix045 (identical sub-mesh topology), compressed to 2.25 m × 1.13 m | not a separate entity (it would duplicate the coil) |

| Entity | Size (w × h × l) | Triangles (LOD) | Sections | Colliders |
|---|---|---|---|---|
| Barbed wire: concertina coil (3.9 m) | 0.65 × 0.86 × 3.88 m | 23,696 (7,104), from 473,988 | 3 | player-only coil volume |
| Barbed wire: post fence (4.7 m) | 0.18 × 1.06 × 4.68 m | 44,816 (13,426), from 1,793,208 | 4 | 4 solid posts + player-only strand volume |
| Barbed wire: knife rest (2.3 m) | 1.32 × 1.22 × 2.29 m | 23,732 (7,114), from 474,720 | 2 | 4 solid X-legs + ridge beam, player-only volume |

**Fence gap.** The source fence's two runs do not meet: the first run ends 0.29 m short of the second run's first post. At bake, the first run's islands slide +0.33 m (a `meshEdits` translate, no geometry added), so its strands reach that post's face.

**Traversal.**
- The wire volumes stop the player: `defense.traversal: "block"`, the default for all three.
- `"slow"` is the data alternative: there is no volume, and the speed is multiplied by `slowFactor` (0.35) while the feet are inside. It is tested.
- `unsafeVault` refuses any hold-Space vault whose crossing passes within 1 m of an uncut wire obstacle's bounds. The test vaults a low wall beside the coil: refused, then allowed once the coil is moved away.
- There is no health model, so the wire does not injure. It only obstructs.

**Rounds and thrown objects.** Rays ignore the player-only volumes, so shots pass between the loops and strands. Posts and timbers are solid and stop rounds like any timber.

**Damage.**
- Small arms never cut the wire (`smallArmsMultiplier` 0).
- A blast damages each section by its own distance (`sectionHp` 60 against the shared falloff). A section that reaches 0 is cut: its slab of the model is hidden and its colliders, solid and player-only, are removed. The rest stands.
- Test: a grenade-sized blast at the fence's end cuts 1 of 4 stretches.
- All sections cut = destroyed. A cut produces a metal impact effect.
- Cuts are a session overlay, reset on F5. Defence damage is not written by "Apply session changes" (existing behaviour for defences).

**Costs.**
- A placed obstacle shares one baked model, so there are no per-instance copies.
- Beyond 30 m the LOD draws (about 30 % of the triangles); nothing draws beyond 350 m.
- Disk: 2.7 / 5.4 / 3.7 MB including LODs and textures.

**Known limits.**
- Severed wire leaves no hanging remnant mesh: the cut stretch disappears, and the posts and strands of the neighbouring stretches remain.
- There is no rubble or wire debris beyond the impact effect.
- The audio cue is the existing metallic `ping` until the audio mapping (A1).

## E2 Tank trap ×1.25

**Size.** The existing `def_tank_trap` (no new type) is 1.25× in every linear dimension; asset version 1 → 2:
- beams 1.4 → 1.75 m
- flanges 100 → 125 mm
- crossing 0.43 → 0.54 m above the ground
- overall about 1.21 × 0.87 m → 1.51 × 1.09 m
- collision hull 1.1 → 1.375 m
- footprint 1.4 → 1.75 m

**The tipped wreck.** It pivots on the asset's own crossing height instead of the old hard-coded 0.43 m.

**Factor.** 1.25 is a tuning assumption, as the package asked. Beside the 1.8 m player capsule the hedgehog now reaches hip height. It is about the height of the wire fence, and taller than the concertina coil.

**Migration.**
- Instances carry no scale of their own, so the size lives only in the asset. A map saved against v1 adopts v2 on load, applied once.
- Position (ground pivot) and yaw are kept.
- The instance's stored version is bumped and a toast counts the updated placements.
- Test, on the village example's traps saved as v1:
  - loaded as v2 with the hull top 1.09 m above the pivot;
  - saved and reloaded: identical height and position (no second growth);
  - a new placement is v2, and undo removes it.

**Unchanged.** Small-arms immunity (100 rifle hits) and HE damage (`--selftest-env`). Vehicle blocking is still the `blocksVehicles` flag only: no vehicle simulation is claimed.

**Overlap.** Old layouts that packed hedgehogs closer than 1.75 m now overlap slightly. Placement checks only apply to new placements.

## E3 Mk 2 and M24 grenades

**Where they live.** Both are items in the existing weapon roster: `content/weapons/mk2_grenade.json` (side US) and `m24_grenade.json` (side DE), both with the supplied model. They sit in the sandbox loadout beside the firearms; Q cycles to them.

**Controls.**
- **LMB** throws the grenade in hand.
- **H** (rebindable `grenade` in `content/settings/bindings.json`; G is taken by the M2 pack-up) throws your side's grenade from any weapon, then returns to that weapon. A knife or other side-less item counts as US.

**Count.** Each grenade type is 1 in hand + 3 in the pouch, kept like ammunition: they are counted down, the hand refills from the pouch after the ready time, and the counts reset with the session.

**Throw.**
- Each press throws once: holding the button never throws a second.
- A wind-up (0.32 s Mk 2, 0.38 s M24) is followed by release. **The fuse starts at release**; there is no cooking.
- Swapping during the wind-up cancels the throw: nothing spawns and the count is unchanged.
- The release velocity is the aim lifted by the lob angle, at the throw speed (×0.85 crouched, ×0.6 prone), plus the player's own motion.
- The spawn point is pulled back along the eye ray if a wall is in the way, by the grenade's own size, so it never starts inside a wall. Test: thrown face-on at a wall from 0.3 m, it spawns 0.22 m in front.

**Flight.**
- The grenade is a small Jolt dynamic body, a box fitted to the model, with linear-cast continuous collision (no tunnelling).
- It has per-grenade restitution and friction, plus linear and rolling damping, so it settles instead of rolling on. World gravity is the normal 9.81 m/s²; the player's reduced gravity does not apply to it.
- It is drawn with the supplied model and tumbles end over end.

**Detonation.**
- It goes off exactly once, when the fuse runs out (the timer advances with frame time; measured 4.48 s for a 4.5 s fuse).
- The blast goes through the shared `explode()` path:
  - the grenade wall stamp (chips only; no breach in the test);
  - a small crater (0.13 m deep in the test against the HE shell's 0.62 m);
  - glass, leaves, trees, defences and wire (a Mk 2 beside the coil cuts its stretches);
  - the blast-exposure readout.
- The explosion effect and sound are the existing ones.

| Game tuning (not historical data) | Mk 2 | M24 |
|---|---|---|
| fuse (from release) | 4.5 s | 4.5 s |
| throw speed, lob | 15 m/s, 10° | 17.5 m/s, 12° (the handle carries it farther) |
| restitution / friction | 0.30 / 0.6 | 0.25 / 0.7 |
| blast damage, radius | 80, 5.0 m | 85, 4.5 m |
| measured throw (standing, flat ground, to rest) | 19.3 m | 22.9 m |
| Mk 2 crouched / prone | 14.5 m / 8.2 m | |

**Limits.**
- The supplied GLBs have no animations, so the hold and throw motion is procedural: the hand draws back and releases.
- There is no actor health in this engine, so grenades damage the world only.
- There is no fragmentation simulation beyond the blast radius and falloff.
- Network authority does not apply: the engine is single-player.

## E4 Gravel, dirt and concrete paths

**Entities.** `path_gravel`, `path_dirt` and `path_concrete` are ordinary placeable instances (catalogue category "Paths").
- **Size.** Length and width are per instance (`extra.path`) and editable in the Selection panel: 1–60 m long, 0.8–8 m wide. Defaults are 2 × 8, 3 × 10 and 2 × 8 m.
- **Editing.** They save, load, undo and apply like any placement.
- **Placement.** A path placed near a free end of another snaps on in line.
- **Overlap.** Paths may overlap anything and drape over any slope.

**Draping.**
- The surface is a 0.5 m grid in the path's own frame, draped on the current ground: 1.5–3.5 cm above it for soft paths (lower at the ragged edges), 4.5 cm for concrete.
- It has no collider: you walk on the ground itself, so there are no steps, no floating edges, no vault obstacles and no z-fighting.
- Any terrain change bumps a ground revision, and every path re-drapes. Dirt and gravel therefore follow fresh craters, and no path can ever restore cratered ground. Test: an HE shell lowers the ground 0.60 m under the gravel path, and the path stays 1.5–3.5 cm above the new ground.

**Concrete slabs.**
- Concrete is laid in 2 m slabs. A blast that reaches a slab hard enough breaks it: the slab disappears, the crater shows and the surface lookup there becomes bare ground. The rest survive.
- Test: HE on the first slab breaks 2 of 4 slabs.
- Grenades do not break slabs.

**Materials.** All three are procedural in the main shader (patterns 12–14), in path metres, so resizing never stretches them. Each has real bump-normal and roughness variation:
- **Gravel:** Voronoi stones at two sizes (1.5–4 cm), grey, ochre and flint tints, soil between the stones, patches of coarser stone, and a ragged soil and grass margin.
- **Dirt:** compacted soil with fine crumb and clods, two worn wheel ruts 1.5 m apart, grass in the centre strip, damp darker patches (lower roughness) and grass creeping in at the edges.
- **Concrete:** slab seams every 2 m, exposed aggregate, chipped seam edges, staining and per-slab tone variation, with a soil line where it meets the ground.

These are original procedural materials, not photographic textures. They hold up at play distance; close up they are clearly generated.

**Grass and surfaces.**
- Grass is cleared on the trodden surface only, with a 5 cm margin; the verges keep theirs. Test: 0 of 9 samples on the track, 9 of 9 at 0.6 m beyond the edge.
- Paths and wire obstacles no longer protect the ground from craters. Building footprints still do.
- Surface type:
  - Footsteps: gravel and concrete use the stone step; dirt uses the soft one.
  - Bullet impacts: concrete and gravel give stone chips; dirt gives a ground puff.
- There is no friction model for the player, so surfaces do not change grip.

## E5 Knife and 8.8 cm Flak 18

**Knife** (`knife_bayonet.json`, the supplied model, 0.40 m).
- It is a standalone melee item in the roster. It is **not** fitted to any rifle: this engine has no bayonet-socket system.
- LMB strikes: a 0.12 s wind-up, then one sweep of 5 eye rays over a 40° arc within 1.7 m.
- The first hit on each ray counts, and each target is hit once per swing. Walls end the ray, and a swing never continues through anything.
- A hit acts through the bullet-impact rules with a slow, heavy blade: glass breaks; doors, shutters, trunks and defences register a hit. The impact effect and sound follow the surface.
- Cooldown is 0.6 s.
- Tests: a held button gives one swing; the kitchen window breaks from 0.9 m; open air hits nothing.
- There is no actor health, so there are no combat-damage claims.

**Flak 18** (`gun_flak18`, the supplied model).
- **Model.** A crewed German emplacement on the existing station framework. The supplied model's 153 meshes are split by name family onto the station transforms:
  - cruciform base, outriggers and jacks: fixed;
  - top carriage, sights, handwheels, seats and shield: traverse;
  - cradle, recuperator and equilibrators: elevation;
  - barrel: recoil.

  The base never moves with the barrel (tested).
- **Crewing.**
  - Aim from the gunner's position uses a direct-fire sight (12° field).
  - It traverses a full 360° and elevates from −3° to +85° (tested at both stops).
  - X selects HE or AP for the next reload.
  - It reloads in 5 s and recoils 1.0 m.
  - It is a **fixed** emplacement: Move is refused.
- **Firing.**
  - Shells use the existing ground-target projectile path. They start on the bore axis and are swept from there, so a wall at the muzzle still stops them.
  - HE: 820 m/s, damage 150, radius 2.8 m, crater ×1.15.
  - AP: 1,000 m/s, using the existing AT-shell rules.
  - Its report is a deeper, longer synthesized discharge (`flak_fire`) than the PaK's, standing in until the audio mapping (A1). The muzzle effect is 1.4× the PaK's.
- **Damage.** The AT-gun rules apply: a direct AT-gun, Bazooka or Panzerschreck hit wrecks it in one shot (drawn blackened, barrel slumped), and near misses use the separate splash rule.
- **Not modelled:** no air targets, radar or time fuzes (as the package asked). The 88 mm rounds are not stocked by the existing PaK crate: the emplacement starts with 8 HE + 4 AP and cannot be resupplied yet.
