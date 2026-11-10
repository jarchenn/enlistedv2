# First-person arms (FA1)

The first-person arms are a rigged textured model, "Fps arms" by DJMaesen (CC-BY-4.0, `docs/credits.md`). They replace the box arms of F4; the box arms remain as the fallback if the model is missing.

## Import and rig (reproducible)

```
node tools/rig_fps_arms.mjs <fps_arms.glb> [content/viewmodel]
```

The source is one static mesh: two arms, no skeleton, about 3.4 k vertices, cm units, three 1024 px textures (albedo, metal-roughness, normal). The tool does the following:
- It bakes the node transforms, converts to metres, and splits the two arms (the two connected components, either side of x = 0).
- It places landmarks per arm:
  - **wrist:** the bare-skin strip between cuff and glove;
  - **elbow:** one forearm (0.26 m) behind the wrist;
  - **finger landmarks:** knuckle and mid-finger slices;
  - **thumb:** the inner digit;
  - **index finger:** the top of the fingers.
- It builds 9 bones per arm (right 0-8, left 9-17), with smooth weights:
  - **upper arm** (the stub behind the elbow), blending across ±4 cm at the elbow;
  - **forearm** and **forearm twist**: the twist weight rises from the elbow to the wrist, so the hand's roll spreads along the forearm;
  - **hand**, blending across the wrist;
  - **fingers** proximal and distal (middle to pinky);
  - **index** proximal and distal;
  - **thumb**.
- It recolours the sleeve per faction from its own cloth texture: US olive drab and German field grey, the same albedos as the old box sleeves. The gloves and the bare skin at the wrist keep their texture.
- It writes `content/viewmodel/fps_arms_us.glb`, `fps_arms_de.glb` (skinned; the inverse binds are the rest frames) and `fps_arms.json` (the rest landmarks).

`content/viewmodel` is local-only: it is outside the LAN content hash.

## Posing (`src/fps_arms.cpp`, from `viewmodel.cpp` `armDrawItems`)

- **Targets:** the same as the box arms:
  - the weapon's grip sockets, moved with sway, recoil, ADS, bolt, reload and sprint (`vmLocal`);
  - the support hand goes to the feed during reloads;
  - swim strokes and mantle grips.
- **One-handed:** when a weapon's two sockets coincide (knife, grenades), or a weapon's left spec says `"free": true`, the support hand waits low at the side, mostly off-screen.
- **Hand frames:** each hand has a weapon-space frame from `content/settings/fps_arms.json`:
  - `fwd`: wrist to knuckles;
  - `palm`: the outward normal;
  - `contact`: the grip socket in the hand frame;
  - `curl`: fingers, index and thumb curl in degrees;
  - `soften`;
  - per-weapon overrides.

  Off the weapon (reload, strokes, ledges), the hand slerps toward a relaxed or flat pose and the fingers open.
- **IK:** two-bone IK from the shoulder to the **wrist** (upper arm `kUpperArm`, forearm the model's 0.268 m; pole down and out). The hand then turns `soften` of the way toward the forearm and the wrist is solved again, so the wrist does not break.
- **Skinning:**
  - each bone's frame is built from the solved joints: the elbow-out direction gives the upper arm and forearm roll, and the hand's pinky side gives the twist roll;
  - skin = posed frame × inverse bind;
  - the fingers rotate about the rest knuckle / mid landmarks, and the thumb about the hand axis.
- **Drawing:** a compute skin (`CharInstance`) writes the posed vertices. They are drawn as a textured part of the weapon (`Renderer::viewModelTex`): the same pass, lighting and shadows as the weapon.
- **Visibility:** Settings → Show arms and the scoped-view hiding apply as before. Third person and LAN are unchanged (other players still use the soldier rig).
- **Checks:** `NMK_ARMS_SHOTS=1 --selftest-fps` shoots the Garand, Kar 98k, StG 44, MG 42, knife and Mk 2 at hip, ADS and mid-reload (`selftest_out/fa1_*`).
