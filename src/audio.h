#pragma once
#include "common.h"

// Procedurally synthesised sounds (no external audio files), spatialised via miniaudio.
class Audio {
public:
  bool init();
  void shutdown();
  void play(const std::string& name, vec3 pos, float volume = 1.0f);
  void setListener(vec3 pos, vec3 fwd);
  void setAmbience(float wind, float leaves);
  bool ok() const { return ok_; }
  int playCount = 0;
  std::string lastPlayed; vec3 lastPos{0};
private:
  bool ok_ = false;
  struct Impl; Impl* p = nullptr;
};
