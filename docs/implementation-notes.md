# Implementation notes: destruction, ordnance and environment update

The brief asked for the existing function names to be located before changing them. Every name below was checked against the source at the time of this update (`git grep` on the final commit).

## Where the update hooks into the existing code

| Concern | Existing function (file) | What changed |
|---|---|---|
| Wall generation | `wall()` in `tools/parts.mjs` | New `panel` option. The wall band between openings is split into bays (`panelRects`). `emitPanel` writes each bay's intact, damaged and breached geometry plus its frame collar, with a seeded RNG so regeneration is stable |
| Large buildings | `largeBuilding()` in `tools/catalog_large.mjs` | Passes panel configs per floor and side. The same applies in `catalog_small.mjs`, `catalog_bunkers.mjs` and `build_assets.mjs` |
| Asset loading | `AssetLibrary::loadOne`, `appendPart` (`src/assets.cpp`) | Parses `panels[]` (ID, family, health, bounds, normal, three state meshes), `station` and `crate`. The meshes are built once per asset with `GpuMesh::upload` |
| Collision shapes | `Physics::buildShape` (`src/physics.cpp`) | Unchanged for the immutable core. New `Physics::buildCompound` / `setShape` build the replaceable wall compound |
| Runtime instances | `App::buildRT`, `Physics::addStatic`, `Physics::addKinematic`, `App::updateLeafBodies` (`src/app.cpp`) | `buildRT` now also creates the per-copy wall body (user data `id<<16 | 0xFFFF`) from current panel states. Leaves keep `id<<16 | (k+1)`, and the core keeps `id<<16` |
| Mode switch | `App::enterExplore` / `App::exitExplore` (`src/explore.cpp`); F5 in `App::updateCreative` (`src/editor.cpp`) and in Explore | `exitExplore` calls `resetDamage`, `resetGround`, `resetCombat` and `resetWeapons`. Explore damage is a session overlay |
| Undo | `App::snap` / `App::restore` / `App::pushUndo` (`src/app.cpp`) | `Snapshot` carries `terrainDeltas`. Panel overrides ride on `Instance::panelHp` |
| Persistence | `mapToJson` / `saveMap` / `loadMap` (`src/mapdoc.cpp`) | `overrides.panels` per instance, and `terrainDeltas {templateVersion, fineN, tiles}` per map |

## New systems

| System | Entry points |
|---|---|
| Panel damage | `App::explode`, `App::panelStateFor`, `App::rebuildWalls`, `App::resetDamage`, `drawDamageDebug` (F4) in `src/damage.cpp` |
| Deformable ground | `Terrain::activate`, `Terrain::crater`, `Terrain::rebuildMesh`, `Terrain::deltas` / `applyDeltas` (`src/terrain.cpp`); `App::groundWeight`, `craterAt`, `deformGround`, `syncGroundTiles`, `resetGround` (`src/ground.cpp`) |
| Apply damage to map | `App::applyDamageToMap`, `App::clearAppliedDamage` (`src/ground.cpp`, Edit menu) |
| Projectiles and effects | `App::fireProjectile`, `App::stepProjectiles` (`src/station.cpp`); `Fx` pool (`src/fx.cpp`) |
| Crew stations (PaK 40, M2HB) | `occupyStation`, `switchStationPosition`, `stepStation`, `carriageFits`, `crateTransaction` (`src/station.cpp`) |
| Personal weapons | `resetWeapons`, `selectWeapon`, `stepWeapons`, `resupplyPersonal`, `weaponCamera`, `weaponDrawItems` (`src/weapons.cpp`) |
| GLB weapon models | `bakeGlbModel`, `importWeaponModels`, `GpuModel::load` / `animAt` (`src/model.cpp`); `App::writeWeaponSheets` (`src/weapons.cpp`); `Renderer::drawModels`, `renderModelSheet` |
| First-person handling | `App::stepViewModel`, `vmTarget`, `vmLocal`, `sightLineWorld`, `vmApparent`, `dofParams`, `armDrawItems`, `solveTwoBone` (`src/viewmodel.cpp`); viewmodel pass and the `kDofFS` pass in `Renderer::render` |
| Surface damage and penetration | `App::bulletImpact`, `locateLeaf`, `breakGlass`, `detachLeaf`, `blastLeaves`, `resetLeafDamage` (`src/ballistics.cpp`); `Physics::addDynamic`, `castRayOnly` |
| Craters and AT round | `Terrain::crater` (formula profile), `App::deformGround` (`blastAT`); `StationRT::selAT` / `reserveAT`, `crateTransaction` (`src/station.cpp`) |
| Trees and clouds | `buildTreeAsset`, `App::fellTree`, `treeImpact`, `blastTrees`, `resetTrees`, `treeDrawItems` (`src/foliage.cpp`); `kLeafVS` / `kLeafFS` and the `kSkyFS` clouds (`src/render.cpp`); assets from `tools/gen_env_assets.py` |
| Defences and builder | `App::stepBuilder`, `snapDefense`, `groundSlopeDeg`, `defenseBulletHit`, `blastDefenses`, `blastOcclusion`, `blastPlayer`, `removeSessionPlacements` (`src/builder.cpp`); the defence rule in `App::checkPlacement` |
| Instanced grass | `Grass::update` / `draw` / `build` / `signature` / `contentSignature` (`src/grass.cpp`); `App::updateGrass`, `grassFootprints` (`src/app.cpp`) |
| Recording | `Recorder` (`src/recorder.cpp`), MJPEG AVI. Driven by `--record-range` (`src/selftest_record.cpp`) |

## Decisions and their reasons

- **Base terrain is one heightfield body per tile, not one big one.** Jolt's per-vertex holes left corner triangles under a crater, so the physics floor sat up to 0.68 m above the rendered bowl. With a per-tile body, swapping the base tile for a fine patch body matches render height to within 1 mm.
- **The tile discard mask is written as 255, not 1.** It is a normalised `GL_R8` texture. Writing 1 read back as 0.004, so the base mesh was never hidden and covered every crater bowl; collision was unaffected. This was found by the grass work in M5 and fixed in `Terrain::setMask`.
- **Grass placement only depends on what is actually under it.** A fine cell whose four samples are unchanged reads the base surface (`groundH` in `grass.cpp`). Activating a deformation tile therefore never moves grass, and a crater rebuilds only the 1–2 chunks whose soil changed.
- **Applying damage changes the crater's soil mask slightly.** Persisted deltas store heights only. After a reset, soil is re-derived from depth, so a crater restored from the map differs in its soil edge, and its grass, from the live one. Every reset restores that same persisted state, and the self-test checks this.
- **Aiming parallax.** The PaK muzzle sits 3.1 m in front of the traverse pivot. A scripted lay computed once from the start position misses a 20°-off-axis target by about 1 m. The recording script re-lays until settled, and a human gunner does the same through the sight.
- **Crate messages.** When nothing moves, the emplacement's reason comes first: out of reach, crate empty or already full. The personal-weapon refusal follows it. Before this change, a rifle's "wrong feed type" line hid the reason the PaK could not be served.
