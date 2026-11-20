#pragma once
#include "common.h"

// Sounds spatialised via miniaudio. Events named in content/audio/manifest.json play samples extracted locally from
// the user's FMOD banks (tools/fsb5.py, git-ignored content/audio/samples): several variations per event, never the
// same one twice running, a "_far" layer beyond 90 m when the manifest has one, and automatic fire handled so
// overlapping shot bodies do not pile up (a new shot of the same event fades the previous one out in 40 ms; the
// recorded tail is played once on release). Anything without samples falls back to the procedural sounds.
class Audio {
public:
  bool init();
  bool silent = false;   // TST1: set before init: the null backend (mixing, events and timing as usual; nothing reaches the speakers)
  int loadEvents(const fs::path& dir);   // manifest + samples; returns the number of events with audio
  bool hasEvent(const std::string& name) const;
  bool hasSound(const std::string& name) const;   // VH8: an event or a procedural buffer (what play() can resolve)
  int loadCustom(const fs::path& dir);   // user-supplied mp3s (content/audio/custom): footsteps, door / window opening, zombie; returns files used
  int eventVariant(const std::string& name) const;   // last variation index played (tests), -1 none
  int eventsLoaded = 0, samplesLoaded = 0, samplesMissing = 0;
  std::string lastEvent;   // event or procedural buffer actually used by the last play()
  std::map<std::string, int> eventPlays;   // plays per resolved event (tests)
  void shutdown();
  void play(const std::string& name, vec3 pos, float volume = 1.0f, float pitch = 1.0f, float delay = 0);   // SND1: pitch (1 = as recorded); SND4: delay (s) before it starts
  // SND4: a spent case landing `delay` s after a shot at a shooter's feet (`soft` ground: quieter): within 15 m of the
  // listener only, at most one per 80 ms per shooter (automatic fire) and 6 at once
  void casing(vec3 feet, int shooter, float delay, bool soft);
  int casingsPlayed = 0, casingsSkipped = 0;   // tests
  // SND5 diagnostics (NMK_AUDIO_FIDELITY: no device, the mix read back): each gun event played as the player's own shot,
  // against its decoded mp3: peak, RMS and 1-4 / 4-10 kHz band energy (first 0.5 s from the onset), logged
  void fidelityReport(const fs::path& customDir);
  void setListener(vec3 pos, vec3 fwd);
  vec3 listenerPos() const { return listener_; }   // SND10: where setListener last put the ears
  vec3 listener_{0};
  void setAmbience(float wind, float leaves);
  void setBed(int slot, const fs::path& file, float volume);
  // BTA: map ambience voices (map_ambience.cpp): a looping file per id, decoded on first use; spatial ones (mono) at pos,
  // full level within minD, silent beyond maxD (linear); the others are beds. Volume x the Ambience setting, ducked under
  // nearby gunfire like the beds; 0 keeps the voice loaded but silent. Cheap to call every frame
  void ambVoice(const std::string& id, const fs::path& file, bool spatial, vec3 pos, float volume, float minD = 2, float maxD = 40);
  void ambClear();   // drop every map ambience voice (another map)
  std::vector<std::string> ambIds() const;   // the loaded map ambience voices (tests)
  // SND13: the low-health heartbeat (custom/fx/heartbeat.mp3, trimmed to whole beats): in the head, Effects bus; level
  // 0..1 (ramped: in over 0.4 s, out over 0.8 s), rate = pitch / tempo. Call every frame (0 to stop)
  void setHeartbeat(float level, float rate);
  // SND21: the main-menu track (content/audio/custom/music/menu_title.mp3, streamed, looping): true while the menu shows (cheap to
  // call every frame); in over 1 s, out over 1 s (the fade runs on the audio thread, so it finishes while a load blocks the frame)
  void setMenuMusic(bool on);
  float musicGain = 0.6f;            // mix.json "music" (x master)
  float heartbeatNow = 0;            // the ramped level (tests)
  float heartbeatGain = 1, hurtGain = 1;   // mix.json "heartbeat" / "hurt"
  int hurtPlays = 0;                 // tests: hurt sounds started (the player's)   // a looping music / room-tone file under everything, two slots that mix (empty path: none); cheap to call every frame
  // volume settings (settings.cpp): master on the engine; effects on every spatial play / loop; UI on events whose
  // name starts with "ui_" or whose manifest kind is "ui"; ambience on the wind / leaf beds
  void setGains(float master, float effects, float ui, float ambience);
  // under water (swim.cpp): 0 normal .. 1 muffled (a low-pass on everything that reaches the speakers, a little quieter)
  void setUnderwater(float k);
  void setRoom(float indoor);   // SND5: 0 outdoors .. 1 tunnels / Backrooms: the gun reverb's slap-back and tail
  float underwater = 0;
  float masterGain = 1, effectsGain = 1, uiGain = 1, ambienceGain = 1;
  float gunfireGain = 1;   // UI1b: Settings > Audio "Gunfire" (0..2): gunshots and their tails, cannon / gun fire, shell and grenade blasts
  // looping spatialised voices (tank engines, tracks, turret motors) and runtime-registered sounds (tank.cpp)
  void addSound(const std::string& name, std::vector<float> mono44k);
  int loopStart(const std::string& name, vec3 pos, float volume, float pitch, float offset = 0);   // -1: no voice / unknown sound; offset: 0..1 into the loop
  void loopSet(int handle, vec3 pos, float volume, float pitch);
  void loopStop(int handle);
  int loopsActive() const;
  bool ok() const { return ok_; }
  int playCount = 0;
  std::string lastPlayed; vec3 lastPos{0};
private:
  bool ok_ = false;
  struct Impl; Impl* p = nullptr;
};
