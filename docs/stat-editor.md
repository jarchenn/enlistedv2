# ED2: Guns and Vehicles stat editors

Main menu > **GUNS** (where Controls was) and **VEHICLES** (`src/stat_editor.cpp`, overrides in `src/overrides.cpp`).

- **Lists.**
  - Guns are grouped by the weapon JSON: `side` "blaster" → Blasters, `modern` / `side` "modern" → Modern, the rest → WWII. Grenades, knife and tools are left out.
  - Vehicles: helicopters → Aircraft; tracked → Tanks; wheeled with truck / Ural / Boxer in the id → Trucks; other wheeled → Cars.
  - Search box. A `*` marks an item the user has changed.
- **Fields** come from the item's own JSON (the shipped file plus its local-only patch). New guns and vehicles appear without code.
  - Geometry and identity keys (`parts`, `materials`, `sights`, `sockets`, `model`, `rig`, `colliders`, notes, ids) are left out.
  - Sub-objects are sections. Machine guns are listed separately by their own `name` (roof, bow / coax, chin ...); also the main gun and ammunition, TOW, mobility, armour, ramming, recoil, reload, ADS, flame, scope.
  - Guns also show their damage-table row (`_damage`: near / far, falloff, class, head cap, penetration) and their visual-recoil row (`_handling`: back, pitch, left, omega, flash, `yawScale`).
  - The helicopter also shows the shared flight tuning (`_helicopter`, settings/helicopter.json).
  - Numbers are drag fields with units from the key (`_m`, `_s`, `_deg`, rpm, m/s, hp ...), clamped to 0 .. 10× the shipped value (negative shipped values: ±10×). Booleans are checkboxes, sound names are text, 2–4 number arrays are vector fields.
- **Overrides** are JSON merge patches. Shipped and local-only files are never written.
  - `content/weapons/overrides_user.json`: `{ id: { ...weapon keys, "_damage": {...}, "_handling": {...} } }`.
  - `content/vehicles/overrides_user.json`: `{ stem: {...}, "_helicopter": {...} }`.
  - Both are git-ignored and applied after the shipped and local files: weapons in `App::buildWeaponMeshes`, damage rows in `damageTableReload`, vehicles in `TankSys::loadDefs`, the helicopter in `tune()`.
- **Controls.**
  - A changed field is orange with a **reset** button; the tooltip shows the shipped value.
  - **Reset to default**: the whole item. **Reset all**: two clicks.
  - **JSON**: the item's patch as text, validated on Apply (must parse, be an object and merge). An error is shown and nothing is written.
  - "OVERRIDES ACTIVE: n" shows how many items are changed.
- **Applying.** Edits are saved when a field is released. Leaving the screen or **Apply now** runs `App::reloadGameplayDefs`: weapons, damage table, helicopter tuning, vehicles, loadout. The pointer-keyed caches (animated viewmodels, BR optic rails) are cleared first. Run it from the menu only, not mid-match.
- **LAN.** The host's values rule damage, which the host computes. Fire rate, recoil and handling come from each player's own file. Vehicles are not replicated, so each player's own values apply.
- Check: `--check-ed2` (`src/check_ed2.cpp`).
