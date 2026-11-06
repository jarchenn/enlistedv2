# Complete update (parts 1–5) and Phase 2 tanks: working checklist

Sources: `Normandy_Complete_Update` (instructions 01–07, ACCEPTANCE, REFERENCES, 12 FMOD banks, 14 GLBs) and `Normandy_Phase_2_Tanks` (PHASE_2_PLAN, ENVIRONMENT_AND_INTERIORS, GROUND_AND_GRASS, ACCEPTANCE, two tank GLBs, sight references). Packages are extracted outside the repo at `%TEMP%/nmk/upd/`; source bytes are never committed (only baked derivatives).

## Decisions taken with the user (chat, 2026-11-06), overriding the packages where they differ

- **Tank durability:** a tank **survives 4 front hits and is destroyed by the 5th**; it **survives 1 side or rear hit and is destroyed by the 2nd**.
- **What counts as a hit:** tank cannons, AT guns and Flak count from every side. Bazooka and Panzerschreck count only from the side or rear (front: visible non-penetrating impact, no damage, no splash leak). Small arms never count.
- **Muzzle flash, tracers and impact effects** keep the original look (the user's earlier revert). The package's "universal barrel origins / tracer join" section (02) is **not** applied.
- **Gun changes use only the GLB models the user supplied**, never procedural geometry Claude created: calibration, sight fixes (BAR rear sight = re-pose / hide a sub-part of the supplied mesh), poses, grenades, knife, Flak. The procedural scopes on M1C and ZF39 are kept and left untouched (user decision).

## Order of work (status kept current)

| # | Workstream | Status |
|---|---|---|
| D1 | Impact-centred cumulative wall damage (damage field per wall bay, union, seams, oblique, layers, deterministic seed, mesh+collision rebuilt together, persistence as events, old-map migration) | **done**: `src/wallfield.cpp`, damage suite 106/106 |
| D2 | Floors / roofs / stairs / furniture / fences on the shared damage interface; blast-to-surface distance + occlusion; bounded debris | partial: piers, corners, lintels and upper floors converted to bays; roofs, stairs, furniture and fences pending |
| D3 | AT craters +25–40 % radius, +20–30 % depth; launchers smaller breaches, craters, prop damage; one-shot AT-gun rule kept | **done**: HE crater 2.0 / 0.62 m, AT 3.9 / 1.5 m, launcher blast 1.1 / 1.3 m |
| G1 | Recoil: no climb cap, stronger roster, posture ×1 / ×0.7 / ×0.2, support only when deployed | **done**: `--selftest-update` |
| G2 | Hold Space to vault (validated sweep, no chaining, no double jump) | **done**: also fixed the body-fit queries (centre-of-mass offset) and the steep-ground step-up exploit |
| G3 | Interaction reach +25 %, full-object / child-collider selection, surface distance, prompt = action | **done** |
| G4 | Firing slits wider / taller, geometry and collision through the full depth | **done**: casemate 1.7 × 0.68 m, OP 1.5 × 0.38 m; shots from 3 weapons pass |
| W1 | Per-weapon sights: Kar98k, Panzerschreck (right-side view), BAR (no tall rectangle), Carbine, Thompson, Garand; smooth ADS audit | **done**: anchors measured on the supplied GLBs; BAR ladder leaf folded and Garand peep opened at bake (`model.meshEdits`, no added geometry); Panzerschreck virtual view line + hip offset + world FOV 62; one ADS blend (`adsTime_s` 0.25, the 14° FOV step removed); `--selftest-sights` all ≤ 2 px ([fps-weapons.md#w1](fps-weapons.md#w1)) |
| E1 | Three wire obstacles from the two wire GLBs | |
| E2 | Tank trap ×1.25 (migration without double scale) | |
| E3 | Mk 2 and M24 grenades (inventory, throw, fuse, bounce, blast) | |
| E4 | Gravel / dirt / concrete path entities | |
| E5 | Flak 18 emplacement; knife | |
| A1 | Audio: FSB5 extraction (if a decoder is available), per-weapon mapping, manifest; otherwise report the dependency | |
| T1–T10 | Tanks: import / adapt, entry / exit, gunner optics, hatch MG, driving, HE/AP state machine, damage rules above, windows / apertures, sound / exhaust, persistence | |
| V1 | Ground and grass, walls and interiors (materials, furniture, room templates) | |
| R | Regression suites, screenshots, measurements, docs, commits per workstream | |

## Damage field design (D1)

- **One field per wall bay** (`Panel`), in the bay's own plane. `u` runs along the bay width (asset x for ±z walls, asset z for ±x walls), `v` along height, and depth along the normal.
- **Cells** are 4 cm by default (`R.destruction.cell`). Each cell stores the depth removed from the front (+normal) face and from the back face. Material survives where `removedFront + removedBack < thickness`. The layers (stone 0.47 m, plaster 0.03 m) come from the bay's intact parts.
- **Impact stamp** at the true contact point:
  - Damage falls off with distance in the wall plane, elongated along the projected shot direction for oblique hits.
  - Masonry removes whole stones, using the same course and block layout the stone shader draws, so breaks follow mortar lines. Timber splits along the grain.
  - Deterministic per-event seed. Removed depth only ever grows (union, never restored).
  - When the front removal passes a material-dependent fraction, the back face spalls, giving perforation.
  - Every bay within the stamp radius gets the stamp, in its own coordinates, so seams and corners stay consistent.
- **Mesh and collision** are rebuilt from the same greedy-merged boxes of surviving cells (depth quantised). Cut faces show the stone or plaster layers, since the material patterns are evaluated in asset space. Collision matches the visual mesh exactly.
- **Derived state** for existing code: intact (no removal), damaged (removal, no through-hole of at least 0.04 m²), breached (through-hole of at least 0.04 m²).
- **Persistence**: per-instance list of damage events `{panel-independent local position, direction, profile, energy, seed}`, replayed in order on load and reset. Old maps' `panelHp` are converted to a central event once.
