#include "recorder.h"
#include <stb_image_write.h>
#include <cstring>

namespace {
void u32(FILE* f, uint32_t v) { std::fwrite(&v, 4, 1, f); }
void u16(FILE* f, uint16_t v) { std::fwrite(&v, 2, 1, f); }
void fcc(FILE* f, const char* s) { std::fwrite(s, 1, 4, f); }
void patch(FILE* f, long pos, uint32_t v) { long here = std::ftell(f); std::fseek(f, pos, SEEK_SET); u32(f, v); std::fseek(f, here, SEEK_SET); }
}

bool Recorder::open(const fs::path& p, int w, int h, int fps_, int quality) {
  close();
  W = w & ~1; H = h & ~1; fps = fps_; q = quality;
#ifdef _WIN32
  f = _wfopen(p.wstring().c_str(), L"wb");
#else
  f = std::fopen(p.c_str(), "wb");
#endif
  if (!f) return false;
  index.clear(); maxFrame = 0;
  fcc(f, "RIFF"); riffSizePos = std::ftell(f); u32(f, 0); fcc(f, "AVI ");
  // hdrl
  fcc(f, "LIST"); u32(f, 4 + 8 + 56 + 8 + 4 + 8 + 56 + 8 + 40); fcc(f, "hdrl");
  fcc(f, "avih"); u32(f, 56);
  u32(f, 1000000 / fps); u32(f, 0); u32(f, 0); u32(f, 0x10);   // us/frame, max bytes/s, padding, AVIF_HASINDEX
  framesPos = std::ftell(f); u32(f, 0);                        // total frames
  u32(f, 0); u32(f, 1); u32(f, 0); u32(f, W); u32(f, H); u32(f, 0); u32(f, 0); u32(f, 0); u32(f, 0);
  fcc(f, "LIST"); u32(f, 4 + 8 + 56 + 8 + 40); fcc(f, "strl");
  fcc(f, "strh"); u32(f, 56);
  fcc(f, "vids"); fcc(f, "MJPG"); u32(f, 0); u16(f, 0); u16(f, 0); u32(f, 0);
  u32(f, 1); u32(f, fps); u32(f, 0);                            // scale, rate, start
  lenPos = std::ftell(f); u32(f, 0);                           // length (frames)
  u32(f, 0); u32(f, 0xFFFFFFFFu); u32(f, 0);
  u16(f, 0); u16(f, 0); u16(f, (uint16_t)W); u16(f, (uint16_t)H);
  fcc(f, "strf"); u32(f, 40);
  u32(f, 40); u32(f, W); u32(f, H); u16(f, 1); u16(f, 24); fcc(f, "MJPG"); u32(f, W * H * 3); u32(f, 0); u32(f, 0); u32(f, 0); u32(f, 0);
  // movi
  fcc(f, "LIST"); moviSizePos = std::ftell(f); u32(f, 0); moviStart = std::ftell(f); fcc(f, "movi");
  rgb.resize((size_t)W * H * 3);
  return true;
}

void Recorder::frame(const uint8_t* px, int sw, int sh) {
  if (!f) return;
  // box filter from the framebuffer (bottom-up RGBA) to W x H top-down RGB
  for (int y = 0; y < H; ++y) {
    int y0 = (int)((long long)y * sh / H), y1 = std::max(y0 + 1, (int)((long long)(y + 1) * sh / H));
    for (int x = 0; x < W; ++x) {
      int x0 = (int)((long long)x * sw / W), x1 = std::max(x0 + 1, (int)((long long)(x + 1) * sw / W));
      uint32_t r = 0, g = 0, b = 0, n = 0;
      for (int yy = y0; yy < y1; ++yy) {
        const uint8_t* row = px + (size_t)(sh - 1 - yy) * sw * 4;
        for (int xx = x0; xx < x1; ++xx) { r += row[xx * 4]; g += row[xx * 4 + 1]; b += row[xx * 4 + 2]; ++n; }
      }
      uint8_t* o = &rgb[((size_t)y * W + x) * 3];
      o[0] = (uint8_t)(r / n); o[1] = (uint8_t)(g / n); o[2] = (uint8_t)(b / n);
    }
  }
  jpg.clear();
  stbi_write_jpg_to_func([](void* ctx, void* data, int size) {
    auto* v = (std::vector<uint8_t>*)ctx; v->insert(v->end(), (uint8_t*)data, (uint8_t*)data + size); }, &jpg, W, H, 3, rgb.data(), q);
  if (jpg.size() & 1) jpg.push_back(0);
  uint32_t off = (uint32_t)(std::ftell(f) - moviStart);
  fcc(f, "00dc"); u32(f, (uint32_t)jpg.size()); std::fwrite(jpg.data(), 1, jpg.size(), f);
  index.push_back({off, (uint32_t)jpg.size()});
  maxFrame = std::max(maxFrame, jpg.size());
}

void Recorder::close() {
  if (!f) return;
  long moviEnd = std::ftell(f);
  patch(f, moviSizePos, (uint32_t)(moviEnd - moviStart));
  fcc(f, "idx1"); u32(f, (uint32_t)index.size() * 16);
  for (auto& [off, size] : index) { fcc(f, "00dc"); u32(f, 0x10); u32(f, off); u32(f, size); }
  long end = std::ftell(f);
  patch(f, riffSizePos, (uint32_t)(end - 8));
  patch(f, framesPos, (uint32_t)index.size());
  patch(f, lenPos, (uint32_t)index.size());
  std::fclose(f); f = nullptr;
}
