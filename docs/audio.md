# Audio (complete update A1)

**Banks and decoding.**
- All 12 supplied FMOD banks were read. Every one is FSB5 with PCM16 samples, so `tools/fsb5.py` reads them directly: no FMOD runtime and no third-party decoder.
- Rates (44.1 / 48 kHz), channels, frame counts and loop chunks come from the FSB5 sample headers.
- At load, the runtime downmixes each sample to mono and resamples it to the 44.1 kHz engine rate.

**Pipeline.**
- `tools/audio_manifest.py <banks dir>[;<dir2>]` writes `content/audio/manifest.json`, a deterministic selection by family name.
- `tools/fsb5.py extract content/audio/manifest.json <banks dir>[;<dir2>] content/audio/samples` converts the selection to 16-bit PCM WAV and records each SHA-256 (of the PCM) in `samples/extracted.json`. Several bank folders can be given, separated by `;`.
- `src/audio.cpp` loads the events at start-up.

**Bigote Sound mod V9 (2026-11-06).**
- The user re-supplied the pack as two zips (Part 1: 5 banks, Part 2: 7 banks). Each was extracted into its own empty folder and only read as data.
- All 12 banks are byte-identical to the banks already mapped, so the earlier events are unchanged.
- This pass filled every remaining slot the game plays: far layers, tank guns, generic foley, the bolt cycle, the smoke pop and wading steps.
- To rebuild the samples from the two V9 folders:

```
python -I tools/audio_manifest.py "<part1 dir>;<part2 dir>"
python -I tools/fsb5.py extract content/audio/manifest.json "<part1 dir>;<part2 dir>" content/audio/samples
```

**Redistribution: not cleared.** The banks are from an Enlisted sound mod, and no reuse permission was supplied.
- `content/audio/samples/` is git-ignored and must be extracted locally from your own copy of the banks.
- Only the manifest and the tools are in the repository.
- Without the samples, every event falls back to the existing procedural sounds.

**Not auditioned by a person.**
- Roles were chosen from the bank's family names and checked against each sample's measured envelope:
  - shot bodies: sharp onset (≤ 0.05 s);
  - tails: energy after 0.3 s;
  - reload foley: short.
- Each mapping carries a status:
  - **named**: the family name matches the weapon;
  - **approx**: a named stand-in for an absent recording;
  - **provisional**: no recording of that weapon exists in the banks.
- Please listen before relying on the mapping. Everything is replaceable in `tools/audio_manifest.py`.

## Playback
- **Variations.** Each event has several variations and never plays the same one twice running.
- **Far layer.** Events with a `_far` layer use it beyond 90 m: every firearm, launcher, gun station and tank gun, and the explosions.
  - Named distant recordings where the bank has them (Kar98k / ZF39, Thompson, MP 40, Panzerschreck, Flak); otherwise the bank's generic distant rifle, automatic or cannon shots.
  - Tails have far layers for the Thompson, MP 40 and MG 42.
- **Bolt actions.** After each Kar98k / ZF39 shot the bolt is worked: open at 25 % and close at 70 % of the bolt time (`bolt`, `bolt_close`). Single rounds load with `clip_in`; the Kar98k's stripper clip with `clip_kar98k`.
- **MG 42 embrasure mount.** Plays the handheld MG 42's events: shot bodies, the tail once on release, the belt reload (`reload_mg42`).
- **Smoke grenade.** Ignites with `smoke_pop` (no blast).
- **Footsteps.** Below the water level the player wades with `step_water`: one recorded stride per step, and the previous stride's water tail fades out over 250 ms. Dry ground keeps the procedural grass / stone / wood steps, because the pack has no dry-ground footsteps.
- **Automatic fire.**
  - One shot body plays per fired round, at the game's real cadence, and a new body of the same gun fades the previous one out in 40 ms, so bursts do not stack or clip.
  - The recorded tail plays once when the trigger is released after a burst.
  - Measured: a 1 s MG 42 burst gives 21 rounds and 21 bodies (13 variations, no repeats), then 1 tail.
- **Reloads.** The start and the completion each play from the weapon's reload pool. The Garand plays its clip sound when the clip seats; the Kar98k has its own clip set.
- **Explosions** are sized: shell, AT shell (`explosion_big`), rocket and grenade.
- **Impacts.** Bullet impacts play a material sound (concrete, dirt, metal, wood) at the contact, within 120 m. The knife plays its own material set.
- **Spatial.** Sounds are spatialised by miniaudio with distance rolloff, from a 48-voice pool (the oldest is stolen when all are busy).
- **Not done.** There is no occlusion or reverb model, and no ambience from the banks: the procedural wind stays.

## Selected samples

| Event | Kind | Status | Samples (bank: index name) | Dur. (s) | Note |
|---|---|---|---|---|---|
| `shot_kar98k` | shot | named | rifle:684 `mauser98k_body_1`, rifle:591 `mauser98k_body_2`, rifle:366 `mauser98k_body_3`, rifle:273 `mauser98k_body_4` (+1) | 4.71-5.20 |  |
| `shot_kar98k_far` | far | named | rifle:134 `mauser98k_e_far_body_1`, rifle:551 `mauser98k_e_far_body_2`, rifle:246 `mauser98k_e_far_body_3`, rifle:150 `mauser98k_e_far_body_4` (+4) | 1.86-2.04 |  |
| `shot_kar98k_zf39` | shot | named | rifle:654 `kar98k_scope_body_1`, rifle:392 `kar98k_scope_body_2`, rifle:229 `kar98k_scope_body_3`, rifle:501 `kar98k_scope_body_4` (+1) | 4.80-4.81 | the bank's scoped-Kar98k family |
| `shot_m1_garand` | shot | approx | rifle:140 `m1_body_1`, rifle:545 `m1_body_2`, rifle:687 `m1_body_3`, rifle:533 `m1_body_4` (+1) | 2.55-2.82 | rifle-bank "m1" family (semi-automatic, no loop): taken as the M1 Garand; the name alone does not prove it |
| `shot_m1c_garand` | shot | approx | rifle:140 `m1_body_1`, rifle:545 `m1_body_2`, rifle:687 `m1_body_3`, rifle:533 `m1_body_4` (+1) | 2.55-2.82 | same rifle as the M1 Garand: shares its family on purpose |
| `shot_m1_carbine` | shot | provisional | mgun:1552 `m1_body_1`, mgun:249 `m1_body_2`, mgun:301 `m1_body_3`, mgun:807 `m1_body_4` (+1) | 2.00-2.00 | no M1 Carbine firing recording exists in the banks (only its reload foley); the automatic-bank "m1" family is used as a distinct placeholder - replace when a carbine recording is available |
| `shot_m1_thompson` | shot | named | mgun:363 `thompson_body_1`, mgun:605 `thompson_body_2`, mgun:1660 `thompson_body_3`, mgun:1659 `thompson_body_4` (+1) | 1.27-1.43 |  |
| `tail_m1_thompson` | tail | named | mgun:1468 `thompson_tail_1`, mgun:95 `thompson_tail_2`, mgun:394 `thompson_tail_3`, mgun:1667 `thompson_tail_4` (+1) | 3.07-3.07 |  |
| `shot_mp40` | shot | named | mgun:252 `mp40_body_1`, mgun:1398 `mp40_body_2`, mgun:439 `mp40_body_3`, mgun:672 `mp40_body_4` (+1) | 0.96-0.96 |  |
| `tail_mp40` | tail | named | mgun:219 `mp40_tail_1`, mgun:1120 `mp40_tail_2`, mgun:1140 `mp40_tail_3`, mgun:1388 `mp40_tail_4` (+1) | 1.44-2.06 |  |
| `shot_stg44` | shot | named | mgun:1109 `stg_44_body_1`, mgun:317 `stg_44_body_2`, mgun:976 `stg_44_body_3`, mgun:861 `stg_44_body_4` (+1) | 1.85-2.09 |  |
| `tail_stg44` | tail | named | mgun:68 `stg_44_tail_1`, mgun:13 `stg_44_tail_2`, mgun:1173 `stg_44_tail_3`, mgun:1647 `stg_44_tail_4` (+1) | 4.35-5.54 |  |
| `shot_m3_grease_gun` | shot | named | mgun:1484 `m3_body_1`, mgun:1333 `m3_body_2`, mgun:1085 `m3_body_3`, mgun:482 `m3_body_4` (+1) | 0.52-0.78 | the automatic bank's M3 family (M3 grease gun) |
| `tail_m3_grease_gun` | tail | named | mgun:258 `m3_tail_1`, mgun:1052 `m3_tail_2`, mgun:214 `m3_tail_3`, mgun:567 `m3_tail_4` (+1) | 2.27-2.90 |  |
| `shot_fg42` | shot | named | mgun:418 `fg42_body_1`, mgun:650 `fg42_body_2`, mgun:72 `fg42_body_3`, mgun:894 `fg42_body_4` (+1) | 3.28-3.80 | plain fg42 family (fg42_m2_* is another labelled variant, not used) |
| `tail_fg42` | tail | named | mgun:484 `fg42_tail_1`, mgun:70 `fg42_tail_2`, mgun:1017 `fg42_tail_3`, mgun:1078 `fg42_tail_4` (+1) | 2.61-3.33 |  |
| `shot_m1918a2_bar` | shot | named | mgun:708 `m1918_body_1`, mgun:836 `m1918_body_2`, mgun:465 `m1918_body_3`, mgun:383 `m1918_body_4` (+1) | 1.37-2.50 | m1918 family (BAR), not the browning_auto_5 shotgun |
| `tail_m1918a2_bar` | tail | named | mgun:1674 `m1918_tail_1`, mgun:863 `m1918_tail_2`, mgun:1231 `m1918_tail_3`, mgun:167 `m1918_tail_4` (+1) | 2.88-3.38 |  |
| `shot_mg42` | shot | named | mgun:961 `mg42_body_1`, mgun:1522 `mg42_body_1`, mgun:610 `mg42_body_2`, mgun:643 `mg42_body_2` (+12) | 0.72-0.72 |  |
| `tail_mg42` | tail | named | mgun:176 `mg42_tail_1`, mgun:1301 `mg42_tail_2`, mgun:1415 `mg42_tail_3` | 4.36-4.44 |  |
| `shot_bazooka_m9` | shot | approx | antivehicle:5 `m1_bazooka_body_1`, antivehicle:44 `m1_bazooka_body_2`, antivehicle:90 `m1_bazooka_body_3` | 4.60-4.95 | labelled M1 bazooka: an approximation for the M9 |
| `shot_panzerschreck` | shot | named | antivehicle:133 `panzerschreck_body_1`, antivehicle:47 `panzerschreck_body_2`, antivehicle:12 `panzerschreck_body_3` | 4.60-4.89 |  |
| `shot_panzerschreck_far` | far | named | antivehicle:38 `panzerschreck_e_far_body_1`, antivehicle:76 `panzerschreck_e_far_body_2`, antivehicle:109 `panzerschreck_e_far_body_3` | 2.66-2.98 |  |
| `shot_m2hb` | shot | named | mgun_mounted:439 `12_7mm_m2_body_1`, mgun_mounted:25 `12_7mm_m2_body_2`, mgun_mounted:207 `12_7mm_m2_body_3`, mgun_mounted:196 `12_7mm_m2_body_4` (+4) | 2.67-3.00 | mounted-bank 12_7mm_m2 (12_7mm_browning50_* is a second candidate, not used) |
| `pak_fire` | shot | approx | cannon:287 `75mm_kwk40_body_1`, cannon:26 `75mm_kwk40_body_2`, cannon:102 `75mm_kwk40_body_3` | 5.77-6.91 | no PaK 40 recording; the 7.5 cm KwK 40 (the PaK 40's tank-gun relative, same calibre and cartridge) is used instead of the 76mm_pak36 family |
| `flak_fire` | shot | approx | cannon:209 `88mm_flak36_e_body_1`, cannon:83 `88mm_flak36_e_body_2`, cannon:272 `88mm_flak36_e_body_3`, cannon:141 `88mm_flak36_e_body_4` (+4) | 2.70-3.28 | 8.8 cm Flak 36 recordings stand in for the Flak 18 |
| `flak_fire_far` | far | approx | cannon:301 `88mm_flak36_e_far_body_1`, cannon:336 `88mm_flak36_e_far_body_2`, cannon:154 `88mm_flak36_e_far_body_3`, cannon:79 `88mm_flak36_e_far_body_4` (+4) | 1.58-2.58 | Flak 36, distant |
| `explosion` | blast | named | fx_impact:104 `shell_mid_explosion_1`, fx_impact:313 `shell_mid_explosion_2`, fx_impact:367 `shell_mid_explosion_3`, fx_impact:219 `shell_mid_explosion_4` (+3) | 3.74-5.12 | shell / HE |
| `explosion_far` | far | named | fx_impact:115 `shell_mid_explosion_far_01`, fx_impact:16 `shell_mid_explosion_far_02`, fx_impact:124 `shell_mid_explosion_far_03`, fx_impact:416 `shell_mid_explosion_far_04` (+6) | 1.84-5.89 |  |
| `explosion_grenade` | blast | named | fx_impact:521 `grenade_blast_1`, fx_impact:418 `grenade_blast_2`, fx_impact:72 `grenade_blast_3`, fx_impact:449 `grenade_blast_4` (+1) | 2.81-3.36 |  |
| `explosion_grenade_far` | far | named | fx_impact:47 `grenade_blast_far_01`, fx_impact:598 `grenade_blast_far_02`, fx_impact:544 `grenade_blast_far_03`, fx_impact:278 `grenade_blast_far_04` (+4) | 4.00-5.00 |  |
| `explosion_rocket` | blast | named | fx_impact:564 `rocket_explosion_1`, fx_impact:520 `rocket_explosion_2`, fx_impact:519 `rocket_explosion_3`, fx_impact:442 `rocket_explosion_4` (+1) | 4.67-4.67 |  |
| `explosion_big` | blast | named | fx_impact:99 `shell_big_explosion_1`, fx_impact:174 `shell_big_explosion_2`, fx_impact:575 `shell_big_explosion_3`, fx_impact:476 `shell_big_explosion_4` | 5.00-5.77 | AT shell / heavy blast |
| `impact_concrete` | impact | named | fx_impact:422 `bullet_hit_concrete_01`, fx_impact:152 `bullet_hit_concrete_02`, fx_impact:221 `bullet_hit_concrete_03`, fx_impact:238 `bullet_hit_concrete_04` (+4) | 1.61-2.45 |  |
| `impact_dirt` | impact | named | fx_impact:172 `bullet_hit_dirt_01`, fx_impact:333 `bullet_hit_dirt_02`, fx_impact:590 `bullet_hit_dirt_03`, fx_impact:23 `bullet_hit_dirt_04` (+4) | 1.00-1.36 |  |
| `impact_metal` | impact | named | fx_impact:467 `bullet_hit_metal_01`, fx_impact:145 `bullet_hit_metal_02`, fx_impact:122 `bullet_hit_metal_03`, fx_impact:589 `bullet_hit_metal_04` (+4) | 0.58-1.00 |  |
| `impact_wood` | impact | named | fx_impact:188 `bullet_hit_wood_01`, fx_impact:168 `bullet_hit_wood_02`, fx_impact:466 `bullet_hit_wood_03`, fx_impact:431 `bullet_hit_wood_04` (+4) | 1.00-1.36 |  |
| `impact_water` | impact | named | fx_impact:482 `bullet_hit_water_01`, fx_impact:319 `bullet_hit_water_02`, fx_impact:509 `bullet_hit_water_03`, fx_impact:443 `bullet_hit_water_04` (+4) | 0.87-1.80 |  |
| `knife_concrete` | impact | named | fx_impact:471 `impact_knife_cocncrete_01`, fx_impact:469 `impact_knife_cocncrete_02`, fx_impact:403 `impact_knife_cocncrete_03`, fx_impact:89 `impact_knife_cocncrete_04` (+1) | 0.61-0.73 | bank spelling kept: "cocncrete" |
| `knife_metal` | impact | named | fx_impact:299 `impact_knife_metal_01`, fx_impact:461 `impact_knife_metal_02`, fx_impact:531 `impact_knife_metal_03`, fx_impact:537 `impact_knife_metal_04` (+1) | 0.50-0.64 |  |
| `knife_wood` | impact | named | fx_impact:381 `impact_knife_wood_01`, fx_impact:562 `impact_knife_wood_02`, fx_impact:227 `impact_knife_wood_03`, fx_impact:366 `impact_knife_wood_04` (+1) | 0.53-0.70 |  |
| `knife_generic` | impact | named | fx_impact:169 `impact_knife_generic_01`, fx_impact:352 `impact_knife_generic_02`, fx_impact:356 `impact_knife_generic_03`, fx_impact:415 `impact_knife_generic_04` (+1) | 0.55-0.72 |  |
| `reload_kar98k` | foley | named | rifle_foley:328 `change_ammo_mauser98k_1`, rifle_foley:199 `change_ammo_mauser98k_2`, rifle_foley:136 `change_ammo_mauser98k_3`, rifle_foley:351 `change_ammo_mauser98k_6` | 0.65-1.37 | stripper-clip reload; stages are not ordered by audition |
| `clip_kar98k` | foley | named | rifle_foley:128 `change_ammo_mauser98k_clip_1`, rifle_foley:312 `change_ammo_mauser98k_clip_2`, rifle_foley:547 `change_ammo_mauser98k_clip_3` | 0.47-0.55 |  |
| `reload_m1_garand` | foley | named | rifle_foley:295 `change_ammo_m1garand_2`, rifle_foley:192 `change_ammo_m1garand_3` | 0.42-0.80 |  |
| `clip_m1_garand` | foley | named | rifle_foley:408 `m1garand_clip_1`, rifle_foley:522 `m1garand_clip_2`, rifle_foley:231 `m1garand_clip_3` | 1.35-1.43 | en-bloc clip: played when the clip seats |
| `reload_m1_carbine` | foley | named | mgun_foley:210 `change_ammo_m1carbine_2`, mgun_foley:75 `change_ammo_m1carbine_3` | 0.46-0.62 |  |
| `reload_mp40` | foley | named | mgun_foley:190 `change_ammo_mp40_1`, mgun_foley:199 `change_ammo_mp40_2`, mgun_foley:98 `change_ammo_mp40_3` | 0.85-1.27 |  |
| `reload_stg44` | foley | named | mgun_foley:281 `change_ammo_stg_44_1`, mgun_foley:41 `change_ammo_stg_44_2`, mgun_foley:179 `change_ammo_stg_44_3` | 0.62-1.14 |  |
| `reload_m3_grease_gun` | foley | named | mgun_foley:59 `change_ammo_m3_1`, mgun_foley:51 `change_ammo_m3_2`, mgun_foley:14 `change_ammo_m3_3` | 0.44-1.07 |  |
| `reload_m1918a2_bar` | foley | named | mgun_foley:173 `change_ammo_m1918_1`, mgun_foley:220 `change_ammo_m1918_2`, mgun_foley:95 `change_ammo_m1918_3` | 0.36-0.59 |  |
| `reload_mg42` | foley | named | mgun_foley:292 `change_ammo_mg42_1`, mgun_foley:1 `change_ammo_mg42_2`, mgun_foley:166 `change_ammo_mg42_3`, mgun_foley:132 `change_ammo_mg42_4` (+3) | 0.73-1.56 | belt; stages are not ordered by audition |
| `reload_m1_thompson` | foley | named | mgun_foley:136 `change_ammo_thompson_1`, mgun_foley:289 `change_ammo_thompson_2`, mgun_foley:320 `change_ammo_thompson_3` | 0.59-1.50 | box-magazine Thompson (drum reloads not used) |
| `reload_fg42` | foley | named | mgun_foley:265 `change_ammo_fg42_1`, mgun_foley:169 `change_ammo_fg42_2`, mgun_foley:330 `change_ammo_fg42_3` | 0.61-0.75 |  |
| `bolt` | foley | approx | rifle_foley:68 `shutter_out_rifle_bolt_1`, rifle_foley:99 `shutter_out_rifle_bolt_2`, rifle_foley:407 `shutter_out_rifle_bolt_3`, rifle_foley:103 `shutter_out_rifle_bolt_4` (+1) | 0.39-0.53 | generic rifle-bolt foley for bolt actions (Kar98k / ZF39) |
| `shot_m1_thompson_far` | far | named | mgun:15 `thompson_e_far_body_1`, mgun:150 `thompson_e_far_body_2`, mgun:177 `thompson_e_far_body_3`, mgun:571 `thompson_e_far_body_4` (+4) | 1.68-1.80 |  |
| `tail_m1_thompson_far` | tail | named | mgun:709 `thompson_e_far_tail_1`, mgun:689 `thompson_e_far_tail_2`, mgun:1040 `thompson_e_far_tail_3`, mgun:1559 `thompson_e_far_tail_4` (+4) | 3.84-4.14 |  |
| `shot_mp40_far` | far | named | mgun:1168 `mp40_e_far_body_1`, mgun:174 `mp40_e_far_body_2`, mgun:1147 `mp40_e_far_body_3`, mgun:751 `mp40_e_far_body_4` (+4) | 1.28-1.37 |  |
| `tail_mp40_far` | tail | named | mgun:218 `mp40_e_far_tail_1`, mgun:804 `mp40_e_far_tail_2`, mgun:1548 `mp40_e_far_tail_3`, mgun:1005 `mp40_e_far_tail_4` (+4) | 4.63-4.63 |  |
| `tail_mg42_far` | tail | named | mgun:802 `mg42_e_far_tail_1`, mgun:134 `mg42_e_far_tail_2`, mgun:84 `mg42_e_far_tail_3` | 3.52-3.69 |  |
| `shot_kar98k_zf39_far` | far | named | rifle:134 `mauser98k_e_far_body_1`, rifle:551 `mauser98k_e_far_body_2`, rifle:246 `mauser98k_e_far_body_3`, rifle:150 `mauser98k_e_far_body_4` (+4) | 1.86-2.04 | the Kar98k's distant family: the scope does not change the report |
| `shot_m1_garand_far` | far | approx | rifle:314 `shot_rifle_far_01`, rifle:797 `shot_rifle_far_02`, rifle:112 `shot_rifle_far_03`, rifle:467 `shot_rifle_far_04` (+4) | 4.51-6.52 | no distant recording of this rifle: the bank's generic distant rifle shot |
| `shot_m1c_garand_far` | far | approx | rifle:314 `shot_rifle_far_01`, rifle:797 `shot_rifle_far_02`, rifle:112 `shot_rifle_far_03`, rifle:467 `shot_rifle_far_04` (+4) | 4.51-6.52 | no distant recording of this rifle: the bank's generic distant rifle shot |
| `shot_m1_carbine_far` | far | approx | rifle:314 `shot_rifle_far_01`, rifle:797 `shot_rifle_far_02`, rifle:112 `shot_rifle_far_03`, rifle:467 `shot_rifle_far_04` (+4) | 4.51-6.52 | no distant recording of this rifle: the bank's generic distant rifle shot |
| `shot_stg44_far` | far | approx | mgun:4 `shot_mgun_far_01`, mgun:666 `shot_mgun_far_02`, mgun:257 `shot_mgun_far_03`, mgun:371 `shot_mgun_far_04` (+12) | 0.47-0.55 | no distant recording of this gun: the bank's generic distant automatic shot |
| `shot_m3_grease_gun_far` | far | approx | mgun:4 `shot_mgun_far_01`, mgun:666 `shot_mgun_far_02`, mgun:257 `shot_mgun_far_03`, mgun:371 `shot_mgun_far_04` (+12) | 0.47-0.55 | no distant recording of this gun: the bank's generic distant automatic shot |
| `shot_fg42_far` | far | approx | mgun:4 `shot_mgun_far_01`, mgun:666 `shot_mgun_far_02`, mgun:257 `shot_mgun_far_03`, mgun:371 `shot_mgun_far_04` (+12) | 0.47-0.55 | no distant recording of this gun: the bank's generic distant automatic shot |
| `shot_m1918a2_bar_far` | far | approx | mgun:4 `shot_mgun_far_01`, mgun:666 `shot_mgun_far_02`, mgun:257 `shot_mgun_far_03`, mgun:371 `shot_mgun_far_04` (+12) | 0.47-0.55 | no distant recording of this gun: the bank's generic distant automatic shot |
| `shot_mg42_far` | far | approx | mgun:4 `shot_mgun_far_01`, mgun:666 `shot_mgun_far_02`, mgun:257 `shot_mgun_far_03`, mgun:371 `shot_mgun_far_04` (+12) | 0.47-0.55 | no distant recording of this gun: the bank's generic distant automatic shot |
| `shot_m2hb_far` | far | approx | mgun:4 `shot_mgun_far_01`, mgun:666 `shot_mgun_far_02`, mgun:257 `shot_mgun_far_03`, mgun:371 `shot_mgun_far_04` (+12) | 0.47-0.55 | no distant recording of this gun: the bank's generic distant automatic shot |
| `shot_bazooka_m9_far` | far | approx | cannon:338 `rocket_launcher_e_far_body_1`, cannon:232 `rocket_launcher_e_far_body_2`, cannon:101 `rocket_launcher_e_far_body_3` | 6.00-6.00 | generic distant rocket-launcher shot |
| `pak_fire_far` | far | approx | cannon:294 `cannon_e_m_far_body_01`, cannon:125 `cannon_e_m_far_body_02`, cannon:325 `cannon_e_m_far_body_03`, cannon:296 `cannon_e_m_far_body_04` (+6) | 2.50-4.00 | generic distant medium cannon |
| `tank_17pdr` | shot | approx | cannon:348 `76mm_m1_body_1`, cannon:62 `76mm_m1_body_2`, cannon:82 `76mm_m1_body_3` | 3.56-3.75 | no 17-pounder recording: the US 76 mm M1 tank gun (the nearest Allied 76 mm tank gun) stands in |
| `tank_17pdr_far` | far | approx | cannon:294 `cannon_e_m_far_body_01`, cannon:125 `cannon_e_m_far_body_02`, cannon:325 `cannon_e_m_far_body_03`, cannon:296 `cannon_e_m_far_body_04` (+6) | 2.50-4.00 | generic distant medium cannon |
| `tank_88` | shot | approx | cannon:209 `88mm_flak36_e_body_1`, cannon:83 `88mm_flak36_e_body_2`, cannon:272 `88mm_flak36_e_body_3`, cannon:141 `88mm_flak36_e_body_4` (+4) | 2.70-3.28 | KwK 36: the tank version of the 8.8 cm Flak - shares the Flak 36 family on purpose (no KwK 36 recording) |
| `tank_88_far` | far | approx | cannon:168 `cannon_e_b_far_body_01`, cannon:337 `cannon_e_b_far_body_02`, cannon:16 `cannon_e_b_far_body_03`, cannon:58 `cannon_e_b_far_body_04` (+6) | 2.99-3.98 | generic distant heavy cannon |
| `shot_mg34` | shot | named | mgun:453 `7_92mm_mg34_body_1`, mgun:1131 `7_92mm_mg34_body_2`, mgun:1686 `7_92mm_mg34_body_3`, mgun:347 `7_92mm_mg34_body_4` (+4) | 1.38-1.59 | Tiger I hull / coaxial MG 34 |
| `bolt_close` | foley | approx | rifle_foley:317 `shutter_in_rifle_bolt_1`, rifle_foley:86 `shutter_in_rifle_bolt_2`, rifle_foley:288 `shutter_in_rifle_bolt_3`, rifle_foley:248 `shutter_in_rifle_bolt_4` (+1) | 0.27-0.44 | generic rifle bolt closing: second half of the bolt cycle after each Kar98k / ZF39 shot |
| `clip_in` | foley | approx | rifle_foley:73 `shell_in_rifle_bolt_1`, rifle_foley:186 `shell_in_rifle_bolt_2`, rifle_foley:258 `shell_in_rifle_bolt_3`, rifle_foley:423 `shell_in_rifle_bolt_4` (+1) | 0.80-0.99 | generic: one round pressed into a bolt-action magazine (ZF39 single loading, Kar98k top-off) |
| `belt` | foley | approx | mgun_foley:244 `belt_in_lmg4_1`, mgun_foley:101 `belt_in_lmg4_2`, mgun_foley:196 `belt_in_lmg4_3` | 0.71-0.78 | generic LMG belt loading (M2HB, tank MGs) |
| `mag_out` | foley | approx | mgun_foley:135 `mag_out_smg1_1`, mgun_foley:112 `mag_out_smg1_2`, mgun_foley:328 `mag_out_smg1_3` | 0.42-0.52 | generic SMG magazine out (AI reloads) |
| `mag_in` | foley | approx | mgun_foley:92 `mag_in_smg1_1`, mgun_foley:162 `mag_in_smg1_2`, mgun_foley:145 `mag_in_smg1_3` | 0.54-0.95 | generic SMG magazine in |
| `reload_m1c_garand` | foley | named | rifle_foley:295 `change_ammo_m1garand_2`, rifle_foley:192 `change_ammo_m1garand_3` | 0.42-0.80 | same rifle as the M1 Garand |
| `clip_m1c_garand` | foley | named | rifle_foley:408 `m1garand_clip_1`, rifle_foley:522 `m1garand_clip_2`, rifle_foley:231 `m1garand_clip_3` | 1.35-1.43 | same en-bloc clip as the M1 Garand |
| `smoke_pop` | blast | named | fx_impact:296 `smoke_grenade_expl_1`, fx_impact:7 `smoke_grenade_expl_2`, fx_impact:120 `smoke_grenade_expl_3`, fx_impact:98 `smoke_grenade_expl_4` (+1) | 1.67-1.67 | smoke grenade igniting (no blast) |
| `step_water` | step | named | ambient:51 `footstep_waterdeep_01`, ambient:52 `footstep_waterdeep_02`, ambient:0 `footstep_waterdeep_03`, ambient:87 `footstep_waterdeep_04` (+4) | 2.67-3.68 | one wading stride each (~1.2 s plus a water tail); only used below the water level |

## Gaps reported (no recording in the supplied banks)
Checked against the full sample list of all 12 banks (V9 pass). These slots have no recording, so they keep their procedural sound or use the stand-in shown.

- **Footsteps on dry ground** (grass, dirt, gravel, concrete, stone, wood). The only footsteps in the pack are deep-water wading strides (`footstep_waterdeep`), so dry surfaces stay procedural.
- **Landing, jump, crawl / prone movement, gear rattle while moving.** None: the `cloth_*` / `rattl_*` foley is reload handling. Not played.
- **M1 Carbine firing.** Only its reload foley exists. A distinct provisional family stands in (see the table); distant: the generic distant rifle.
- **Distant fire of the M1 / M1C Garand, StG 44, M3, FG 42, BAR, MG 42 and M2HB.** No named far recordings; the generic distant rifle or automatic shots stand in.
- **PaK 40.** The 7.5 cm KwK 40 stands in; the `76mm_pak36` family was not used. Distant: the generic medium cannon.
- **M9 Bazooka.** The family is labelled "m1_bazooka", an approximation. Distant: the generic rocket launcher.
- **Flak 18.** Flak 36 recordings stand in.
- **Tank guns.** No 17-pounder: the US 76 mm M1 stands in. No KwK 36: the Tiger shares the Flak 36 family. Distant: the generic medium / heavy cannon.
- **Rocket loading** (Bazooka, Panzerschreck). No foley; the procedural clicks stay (`rocket_ready`, `rocket_in`).
- **Gun-station loading** (PaK 40 / Flak 18 breech, ammunition crate). No breech sample (`cannon_m_mech` is a 5-6 s mechanism of unknown role, not used); the procedural `breech` / `crate` stay.
- **M1 Garand en-bloc ping.** No labelled ping; procedural.
- **Dry fire (empty click).** Only a Mosin dry fire exists; the procedural click stays (it is also the UI and grenade-pin click).
- **Grenade pin, throw and knife swing.** No suitable samples; the existing click is used.
- **Weapon switch / draw, ADS cloth, bipod deploy.** None.
- **Tank engines, tracks, turret drive.** Not in these banks (the tank bank holds impacts only); procedural.

## In the pack, but no slot in the game
- Bullet crack / whiz (near misses), ricochets, shell casings falling by surface, tank-armour hits, flesh hits.
- Battle ambience and weather loops.
- The pistol bank (M1911, P38, Walther PP and others) and rifles not in the arsenal (M1903 Springfield, Gewehr 43, Lee-Enfield and others): ready to map when the weapons are added.
