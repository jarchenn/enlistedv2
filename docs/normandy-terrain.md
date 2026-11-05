# Normandy terrain templates: research plan

**Status: not built.** The *Omaha Beach (Easy Red)* and *Normandy Forest* templates are listed in New Map, but disabled. This file is the plan the beach template must follow before any final art.

## Omaha Beach: Easy Red / E-1 draw (terrain only)

- **Primary source.** U.S. Army Center of Military History, *Omaha Beachhead (6 June–13 June 1944)*, CMH Pub 100-11, including its maps and terrain photographs: https://history.army.mil/Portals/143/Images/Publications/Publication%20By%20Title%20Images/O%20titles%20PDF/CMH_Pub_100-11_plus-maps.pdf
- **Scope.** A section around the E-1 draw (St-Laurent-sur-Mer exit) covering the shoreline orientation, wet and dry sand, shingle bank, bluff silhouette and the route inland up the draw.
- **Tide phase.** One fixed phase, to be recorded here with its source, because the exposed beach width depends on the waterline.
- **Wartime structures are intentionally omitted.** The template has no obstacles, strongpoints, landing craft or houses. It reconstructs the **terrain** only.
- **Deliverables before final art.**
  1. A top-down reference overlay (source map, scale bar, template outline).
  2. One comparison from beach level looking toward the draw (photo next to an in-game capture).
  3. A table of approximate scale and any compressed distances, with the reasons for compressing them.

## Normandy Forest (fictional site)

- A fixed seed or a baked placement manifest, so reopening a map restores the same trees.
- Every template tree and bush gets a stable ID. The map format already stores `removedTemplateVegetation[]`, so a cleared tree stays removed after saving.
- Three connected clearings: a large command-post site, a narrower overlook, and a small fallback position. There must be enough level ground in them for one large building and two bunkers.
- Density rises toward the perimeter, while vehicle-width approaches and infantry-width paths between the clearings stay open.
