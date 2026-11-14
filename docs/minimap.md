# 2D minimap (GM2)

A top-down map drawn into any screen rectangle (`src/minimap.h`, `src/minimap.cpp`).

## In play

- **Corner map:** bottom left in Explore, 200 m across, centred on the player (north up). `App::showMinimap` turns it off.
- **Large map:** `M` toggles the whole terrain, centred on screen.

## What it shows

| Layer | Content |
| --- | --- |
| `MM_TERRAIN` | Shaded relief, water and worn ground; outside the build area dimmed. One 512 px texture, rebuilt when the terrain changes. |
| `MM_STRUCTURES` | Footprints of placed assets over 2 m (not trees). |
| `MM_OBJECTIVES` | The running match's objectives (active yellow, captured blue, locked grey) with their letters; outside a match the map's own. |
| `MM_TEAMMATES` | Living soldiers of the local player's team. |
| `MM_CRATES` | Ammo crates (assets with a `crate` block), yellow squares. |
| `MM_PLAYER` | The player: a white arrow along the view direction. |
| `MM_SPAWNS` | Spawn markers (attackers blue, defenders red). Not in `MM_HUD`. |

## For other screens (deploy screen, artillery)

```cpp
MinimapView v = drawMinimap(app, drawList, p0, p1, centre, metres, layers, markers);
```

- `p0`, `p1`: the screen rectangle; the map is clipped to it.
- `centre`: the world x / z at the rectangle's centre; `metres`: metres across its shorter side. `metres <= 0` fits the whole terrain (centre ignored).
- `layers`: `MinimapLayer` bits (`MM_HUD`, `MM_ALL`, ...).
- `markers`: extra `MinimapMarker`s drawn last: position (world x / z), colour, shape (0 dot, 1 square, 2 arrow along `yaw`, 3 crosshair, 4 ring), size in px, optional label.
- The returned `MinimapView` maps between screen and world (`toScreen`, `toWorld`, `contains`): a click at a screen point `s` is the world spot `v.toWorld(s)`.
