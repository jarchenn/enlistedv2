#pragma once
// Minimal Motion-JPEG AVI writer (RIFF AVI 1.0 + idx1). Plays in VLC, Windows Media Player and browsers'
// download viewers; frames are the real framebuffer including the HUD, box-downscaled and JPEG-encoded.
#include "common.h"
#include <cstdio>

class Recorder {
public:
  bool open(const fs::path& p, int w, int h, int fps, int quality = 72);
  void frame(const uint8_t* rgbaBottomUp, int srcW, int srcH);   // glReadPixels layout
  void close();
  bool active() const { return f != nullptr; }
  int frames() const { return (int)index.size(); }
  ~Recorder() { close(); }
private:
  FILE* f = nullptr;
  int W = 0, H = 0, fps = 30, q = 72;
  long moviStart = 0, riffSizePos = 0, framesPos = 0, lenPos = 0, moviSizePos = 0;
  std::vector<std::pair<uint32_t, uint32_t>> index;   // offset (relative to 'movi'), size
  std::vector<uint8_t> rgb, jpg;
  size_t maxFrame = 0;
};
