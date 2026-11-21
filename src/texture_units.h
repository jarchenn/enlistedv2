#pragma once
// Texture units with a fixed owner (proposal, VOT / VFX rev3). New samplers take a unit from here and add a row, so two
// sessions never pick the same one by reading code; Renderer::checkSamplerUnits() (startup and the first map's 30th
// frame) still fails the selftest on a clash. Only kDeform is used through this table so far; the rows above it record
// what the code binds today (from the glBindTextureUnit / glUniform1i calls), so the next new sampler can see the map.
//   0  uShadow (2D shadow, sun cascades)            setLighting          13  scene / post (various)
//   1  uOcc (sky occlusion)                         setLighting          14  uSurf (terrain surface)        Terrain::draw
//   2-5  scene shader / post (various)                                   15  uTileMask (terrain)            Terrain::draw
//   6  uShadowFar (2D shadow)                       setLighting          16  uLShadow (local light atlas)   lighting.cpp
//   7  uDepth (composite)                                                17  uGiVis                         lighting.cpp
//   8-10  composite (scene copy, shafts, ambient)                        18  uTNrm (terrain normals)        Terrain::draw
//   11 uAO (composite, rebound each frame before it) and kDeform below   19  scene (various)
//   12 scene / post (various)                                            20-22 texture library sets; 23 ground paint;
//                                                                        24 terrain cache; 25 macro map; 26 terrain sky
//                                                                        view / horizon (T5 / T6); 27-31 reserved
namespace tu {
// tdeform: the ground deformation buffer (2D, RG16F). Shares unit 11 with the composite's uAO: setLighting binds it only
// for programs where uDefTex is active (terrain, grass, patch plants), never for the composite (which calls setLighting
// after binding its AO to 11), and no program that samples the buffer declares another sampler on 11.
constexpr int kDeform = 11;
}
