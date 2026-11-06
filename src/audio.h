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
  int loadEvents(const fs::path& dir);   // manifest + samples; returns the number of events with audio
  bool hasEvent(const std::string& name) const;
  int eventVariant(const std::string& name) const;   // last variation index played (tests), -1 none
  int eventsLoaded = 0, samplesLoaded = 0, samplesMissing = 0;
  std::string lastEvent;   // event or procedural buffer actually used by the last play()
  std::map<std::string, int> eventPlays;   // plays per resolved event (tests)
  void shutdown();
  void play(const std::string& name, vec3 pos, float volume = 1.0f);
  void setListener(vec3 pos, vec3 fwd);
  void setAmbience(float wind, float leaves);
  // looping spatialised voices (tank engines, tracks, turret motors) and runtime-registered sounds (tank.cpp)
  void addSound(const std::string& name, std::vector<float> mono44k);
  int loopStart(const std::string& name, vec3 pos, float volume, float pitch);   // -1: no voice / unknown sound
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
