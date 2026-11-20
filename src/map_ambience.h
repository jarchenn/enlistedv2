#pragma once
// BTA: per-map ambience from data (docs/map-ambience.md): wind bed and wind zones, looping point / line emitters (creeks,
// flumes, fires), creaks near sheds, footstep surfaces by ground paint (a soft boundary), step splashes. Read from
// content/audio/map_ambience/<template id>.json, overlaid by the map's own extra.audio.
#include "common.h"
class App;

const json* mapAudioConfig(App& app);                       // nullptr: the map has no audio data
void mapAmbienceFrame(App& app, float dt);                  // every frame (silent outside first person / on the menu)
std::string mapStepEvent(App& app, vec3 pos);               // the footstep event the ground paint under pos asks for ("": none)
void mapStepFeedback(App& app, const std::string& ev, vec3 foot);   // splashes at the contact (pooled ripples)
float mapWindGust();                                        // 0..1, the wind's gust signal (audio; the snowfall may follow it)
