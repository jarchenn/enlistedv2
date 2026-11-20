# Blackwater Thaw BT2, Phase 1: atmosphere and the creek-to-court slice (to the approval gate)

Captures: maps self-test, 1920 x 1080, hour 15.3, Medium and High. Before = `E:\nmk\bt1\bt2_trees2\` (054eced3),
after = `E:\nmk\bt1\p1_after3\`, side-by-side pairs in `E:\nmk\bt1\p1_pairs3\` (not in the repo). The six original
bookmarks are unchanged (1-6); the improved set is 7-9 (Slice passage, Slice bridge, Overlook improved).

## Done

- **Horizon band (the boundary).** The sheer 7-17 m rock ring and the 12-16 m rim band are gone. The terrain now steps
  out in layers:
  - a graded apron, +4..11 m at 18-22 m outside the envelope;
  - irregular ridge shoulders, +16..44 m at 62-68 m outside, cut low at the south-west valley exit;
  - asymmetric far ridges at the terrain edge: north 112, other north 88, south-east 66, south-west 54, the exit 16.

  The play limit is now a hidden collision line, `bt_boundary` (4 m boxes along the envelope, nav block). It replaces
  the culvert walls. The rock-face cladding is replaced by outcrops and boulder groups at irregular spacing.

  Re-running the Phase 0 ray trace: the first hits at the envelope are now 33-38 deg slopes, where Phase 0 found a 76-80 deg
  face. No straight curtain shows in any of the six views.
- **Fir massing.** 240 stands of 12-22 overlapping crowns each replace the even sprinkle (firs 2337 -> 3967). Each stand has
  a dark needle floor (`leaf_litter` paint, snow deposit cut by 60 % under it), so it reads as one dark mass. The snow mask
  box now reaches 80-90 m out to cover the stands.

  Checked first: the far LOD's needle atlas already uses the coverage-preserving mip path (`cutoutMips`). But the atlas
  covers only ~10 % of its own area, even at full resolution, so a single far crown cannot read as a mass whatever the
  mips do.
- **Slice (footbridge -> court).**
  - Buildings: a cottage, a second cottage, a low lean-to and a pump annex in a stepped cluster on the court approach.
    The cottage's and annex's doors face the lane, about 4.6 m wall to wall at its narrowest. There is a 2.1 m service gap
    between the two cottages.
  - Dressing: a dark dead tree in the near third of the bridge shot, six bank stones at the footbridge, a gravel margin,
    and the woodpile off the lane.
- **Overlook.** A new bookmark on the quarry top's real south-west lip (G drops about 10 m within 15 m of it), looking at
  the court. Scrub and a chipped rock sit 1-1.5 m in front, and the roof groups lie below in the fore- and midground.
- **Look.**
  - Haze 2.4 -> 1.6 (the uniform veil).
  - fogLayer 0.0015 / 12 m -> 0.004 / 10 m: the valley pocket carries the mist.
  - Clouds 0.95 -> 0.75; weather 0.9 -> 0.8; saturation 0.74 -> 0.8; a less blue tint.
  - Snowfall: near 150 -> 40 flakes, far 2600 -> 1800. Near streaks are 2.5-7 cm, down from 5-16 cm (`kSnowfallVS`; only
    this map uses snowfall).
- **Sky knobs set**, from the lighting session's per-map look keys (they take effect once its change is merged):
  - cloudOpening [-130.7, 11, 20, 0.8], toward the valley exit;
  - cloudScale 2, cloudStretch 3, cloudDir -130.7;
  - sunDisc 0.

## Not done / still visible (honest list)

- **Roof rings.** The concentric moiré on the washhouse roof is unchanged. Two tries had no visible effect and were
  reverted: snow suppressing the cavity/AO term, and roofSlope 0.42 -> 0.22. The rings show on the bare tiles too, so
  they are the roof tile texture or geometry aliasing at distance, not the snow layer. This corrects the Phase 0 guess.
  Next step: inspect the tile roof material's normal/roughness mips (`pair_washhouse_close*.png`).
- **Slopes and trees.** The near slopes are still fairly smooth, and the individual fir crowns still read thin. The
  stands help, but distant forest needs a denser far impostor or card to read as a dark mass.
- **Terrace retaining face.** The 9 m face under the washhouse terrace (72 deg) is untouched.
- **Brief 1.2 surface work.** Not started:
  - the three-state wet ground;
  - sculpted ruts and drifts;
  - roof snow caps;
  - deep window frames;
  - stalk heights.
- **Creek.** Bank shapes and channel width are unchanged; the slice adds only stones and gravel.
- **Light and colour.** No neutral swatch or luma sampling (brief 1.3.1/1.3.4) yet.
- **Movement clip.** None.
- **Timings.** Shared GPU (not exclusive), so not comparable to the earlier runs. 4508 instances; Medium worst 60 ms,
  High worst 95 ms on this run.
