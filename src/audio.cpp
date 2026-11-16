#include "audio.h"
#include <cstring>
#include <miniaudio.h>
#include <random>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <chrono>

namespace {
#include <cstdio>
#include <cstdlib>
#include <cstdint>
constexpr int SR = 44100;
using Buf = std::vector<float>;

struct Rng { std::mt19937 g{1234}; float n() { return std::uniform_real_distribution<float>(-1, 1)(g); } };

void addClick(Buf& b, float t0, float dur, float freq, float amp, Rng& r) {
  int s0 = (int)(t0 * SR), n = (int)(dur * SR);
  float lp = 0;
  for (int i = 0; i < n && s0 + i < (int)b.size(); ++i) {
    float t = i / (float)SR;
    float env = std::exp(-t / (dur * 0.25f));
    lp += (r.n() - lp) * std::min(1.0f, freq / SR * 6.28f);
    b[s0 + i] += (lp * 0.7f + std::sin(6.2831853f * freq * t) * 0.3f) * env * amp;
  }
}
void addCreak(Buf& b, float t0, float dur, float f0, float f1, float amp, Rng& r) {
  int s0 = (int)(t0 * SR), n = (int)(dur * SR);
  float ph = 0;
  for (int i = 0; i < n && s0 + i < (int)b.size(); ++i) {
    float t = i / (float)SR, u = t / dur;
    float f = f0 + (f1 - f0) * u + 18 * std::sin(t * 23.0f);
    ph += f / SR;
    float saw = 2 * (ph - std::floor(ph)) - 1;
    float stick = 0.5f + 0.5f * std::sin(t * 70.0f + std::sin(t * 13.0f) * 3);
    float env = std::sin(3.14159f * u);
    b[s0 + i] += saw * stick * env * amp * 0.25f + r.n() * env * amp * 0.03f;
  }
}
void addClang(Buf& b, float t0, float base, float amp, float decay, Rng& r) {
  const float partials[] = {1.0f, 2.32f, 3.17f, 4.53f, 6.11f};
  int s0 = (int)(t0 * SR), n = (int)(decay * 4 * SR);
  for (int i = 0; i < n && s0 + i < (int)b.size(); ++i) {
    float t = i / (float)SR, s = 0;
    for (int k = 0; k < 5; ++k) s += std::sin(6.2831853f * base * partials[k] * t) * std::exp(-t * (1.0f + k) / decay) / (1 + k);
    b[s0 + i] += s * amp + r.n() * amp * 0.2f * std::exp(-t * 60);
  }
}
void addThud(Buf& b, float t0, float freq, float amp, Rng& r) {
  int s0 = (int)(t0 * SR), n = (int)(0.25f * SR);
  float lp = 0;
  for (int i = 0; i < n && s0 + i < (int)b.size(); ++i) {
    float t = i / (float)SR;
    lp += (r.n() - lp) * 0.05f;
    b[s0 + i] += (std::sin(6.2831853f * freq * t) * 0.8f + lp * 2.0f) * std::exp(-t * 22) * amp;
  }
}
void addSteps(Buf& b, float freq, float bright, float amp, Rng& r) {
  float lp = 0, hp = 0;
  for (size_t i = 0; i < b.size(); ++i) {
    float t = i / (float)SR;
    float x = r.n();
    lp += (x - lp) * bright; hp = lp;
    b[i] += (hp * 1.4f + std::sin(6.2831853f * freq * t) * 0.6f) * std::exp(-t * 28) * amp;
  }
}
void normalize(Buf& b, float peak = 0.9f) {
  float m = 1e-6f; for (float v : b) m = std::max(m, std::fabs(v));
  for (float& v : b) v *= peak / m;
}
Buf makeWind(float secs, Rng& r, float bright, float gustRate) {
  Buf b((size_t)(secs * SR));
  float lp1 = 0, lp2 = 0;
  for (size_t i = 0; i < b.size(); ++i) {
    float t = i / (float)SR;
    lp1 += (r.n() - lp1) * bright; lp2 += (lp1 - lp2) * bright;
    float gust = 0.55f + 0.45f * std::sin(t * gustRate * 6.28f / secs * 3) * std::sin(t * 0.37f * 6.28f);
    b[i] = lp2 * gust;
  }
  // crossfade ends for a seamless loop
  size_t xf = SR / 2;
  for (size_t i = 0; i < xf; ++i) { float a = i / (float)xf; b[i] = b[i] * a + b[b.size() - xf + i] * (1 - a); }
  b.resize(b.size() - xf);
  normalize(b, 0.6f);
  return b;
}
}

struct Audio::Impl {
  ma_engine engine;
  std::map<std::string, Buf> bufs;
  struct Event { float minDist = 1;   /* full volume inside this range (m): big sounds carry */ std::vector<Buf> v; float gain = 1; std::string kind; int last = -1; };
  std::map<std::string, Event> events;
  std::mt19937 pick{20260606};
  vec3 listener{0};
  struct Voice { ma_audio_buffer buf; ma_sound snd; bool used = false; std::string event; double start = 0; };
  Voice voices[48];
  Buf windBuf, leafBuf; float windVol = 0.0f, leafVol = 0.0f;   // the wind bed gave way to the battlefield recording (setBed)
  ma_audio_buffer windAB, leafAB; ma_sound wind, leaves;
  ma_sound bed[2]; ma_audio_buffer bedAB[2]; Buf bedBuf[2]; bool bedOn[2] = {false, false}; std::string bedFile[2]; float bedVol[2] = {0, 0};
  Voice loops[16];
  ma_lpf_node lpf; bool lpfOk = false; float lpfCut = -1;   // under-water muffle: every sound routes through it
  void route(ma_sound& s) { if (lpfOk) ma_node_attach_output_bus(&s, 0, &lpf, 0); }
};

bool Audio::init() {
  p = new Impl();
  ma_engine_config cfg = ma_engine_config_init();
  cfg.sampleRate = SR;
  if (ma_engine_init(&cfg, &p->engine) != MA_SUCCESS) { logf("audio: no device, sound disabled"); delete p; p = nullptr; return false; }
  {   // muffle stage (swim.cpp): open at 20 kHz until the camera goes under water
    ma_lpf_node_config lc = ma_lpf_node_config_init(ma_engine_get_channels(&p->engine), ma_engine_get_sample_rate(&p->engine), 20000.0, 2);
    p->lpfOk = ma_lpf_node_init(ma_engine_get_node_graph(&p->engine), &lc, nullptr, &p->lpf) == MA_SUCCESS &&
               ma_node_attach_output_bus(&p->lpf, 0, ma_engine_get_endpoint(&p->engine), 0) == MA_SUCCESS;
    p->lpfCut = 20000;
  }
  Rng r;
  auto mk = [&](float len) { return Buf((size_t)(len * SR), 0.0f); };
  // wooden door: latch click, hinge creak / closing thud + latch
  { Buf b = mk(1.0f); addClick(b, 0.0f, 0.05f, 2400, 0.8f, r); addClick(b, 0.06f, 0.04f, 1800, 0.5f, r); addCreak(b, 0.12f, 0.75f, 180, 260, 0.6f, r); normalize(b); p->bufs["wood_open"] = b; }
  { Buf b = mk(0.5f); addThud(b, 0.0f, 85, 1.0f, r); addClick(b, 0.03f, 0.05f, 2200, 0.6f, r); normalize(b); p->bufs["wood_close"] = b; }
  // glazed casement: espagnolette bolt + light frame knock + faint glass ring
  { Buf b = mk(0.6f); addClick(b, 0.0f, 0.08f, 3200, 0.7f, r); addClick(b, 0.09f, 0.03f, 4200, 0.4f, r); addClang(b, 0.1f, 2900, 0.05f, 0.08f, r); normalize(b, 0.7f); p->bufs["glass_open"] = b; }
  { Buf b = mk(0.5f); addThud(b, 0.0f, 160, 0.6f, r); addClang(b, 0.0f, 3100, 0.12f, 0.12f, r); addClick(b, 0.05f, 0.06f, 3300, 0.5f, r); normalize(b, 0.7f); p->bufs["glass_close"] = b; }
  // shutters: wooden rattle and a hook
  { Buf b = mk(0.7f); addClick(b, 0.0f, 0.05f, 1500, 0.6f, r); addCreak(b, 0.05f, 0.5f, 300, 380, 0.3f, r); normalize(b, 0.7f); p->bufs["shutter_open"] = b; }
  { Buf b = mk(0.5f); addThud(b, 0.0f, 120, 0.8f, r); addClick(b, 0.02f, 0.04f, 1300, 0.6f, r); normalize(b, 0.8f); p->bufs["shutter_close"] = b; }
  // heavy steel: bolt + grinding swing / clang
  { Buf b = mk(1.6f); addClick(b, 0.0f, 0.1f, 900, 0.8f, r); addCreak(b, 0.15f, 1.2f, 70, 95, 0.9f, r); normalize(b); p->bufs["steel_open"] = b; }
  { Buf b = mk(2.0f); addClang(b, 0.0f, 210, 0.9f, 0.35f, r); addThud(b, 0.0f, 60, 0.8f, r); normalize(b); p->bufs["steel_close"] = b; }
  // footsteps
  { Buf b = mk(0.18f); addSteps(b, 90, 0.08f, 0.8f, r); normalize(b, 0.35f); p->bufs["step_wood"] = b; }
  { Buf b = mk(0.15f); addSteps(b, 140, 0.25f, 0.8f, r); normalize(b, 0.3f); p->bufs["step_stone"] = b; }
  { Buf b = mk(0.2f); addSteps(b, 60, 0.6f, 0.8f, r); normalize(b, 0.18f); p->bufs["step_grass"] = b; }
  // ordnance: PaK 40 discharge (sharp crack + low boom), HE detonation, breech, crate, carriage, dry click
  { Buf b = mk(2.4f); addClick(b, 0.0f, 0.03f, 5000, 1.0f, r); addThud(b, 0.0f, 48, 1.0f, r); addThud(b, 0.02f, 75, 0.8f, r);
    float lp = 0; for (size_t i = 0; i < b.size(); ++i) { float t = i / (float)SR; lp += (r.n() - lp) * 0.02f; b[i] += lp * 3.0f * std::exp(-t * 2.2f); } normalize(b); p->bufs["pak_fire"] = b; }
  { Buf b = mk(3.2f); addClick(b, 0.0f, 0.04f, 3800, 1.0f, r); addThud(b, 0.0f, 34, 1.0f, r); addThud(b, 0.025f, 55, 0.9f, r); addThud(b, 0.06f, 28, 0.6f, r);
    float lp = 0; for (size_t i = 0; i < b.size(); ++i) { float t = i / (float)SR; lp += (r.n() - lp) * 0.012f; b[i] += lp * 3.6f * std::exp(-t * 1.5f); } normalize(b); p->bufs["flak_fire"] = b; }
  { Buf b = mk(3.0f); addThud(b, 0.0f, 38, 1.0f, r); addClick(b, 0.0f, 0.05f, 2500, 0.8f, r);
    float lp = 0; for (size_t i = 0; i < b.size(); ++i) { float t = i / (float)SR; lp += (r.n() - lp) * 0.012f; b[i] += lp * 4.0f * std::exp(-t * 1.4f); } normalize(b); p->bufs["explosion"] = b; }
  { Buf b = mk(0.6f); addClang(b, 0.0f, 620, 0.5f, 0.12f, r); addClick(b, 0.08f, 0.05f, 1800, 0.7f, r); addThud(b, 0.1f, 140, 0.5f, r); normalize(b, 0.7f); p->bufs["breech"] = b; }
  { Buf b = mk(0.6f); addThud(b, 0.0f, 110, 0.7f, r); addClick(b, 0.05f, 0.06f, 1400, 0.5f, r); addClang(b, 0.12f, 900, 0.15f, 0.1f, r); normalize(b, 0.6f); p->bufs["crate"] = b; }
  { Buf b = mk(1.2f); addCreak(b, 0.0f, 1.1f, 60, 80, 0.7f, r); addThud(b, 0.0f, 70, 0.4f, r); normalize(b, 0.6f); p->bufs["gun_move"] = b; }
  { Buf b = mk(0.15f); addClick(b, 0.0f, 0.04f, 3500, 0.8f, r); normalize(b, 0.5f); p->bufs["click"] = b; }
  // small arms: each weapon gets its own crack / body / tail so identical-calibre guns still differ
  auto gun = [&](const char* name, float crack, float body, float tail, float len, float bright) {
    Buf b = mk(len); addClick(b, 0.0f, 0.02f, crack, 1.0f, r); addThud(b, 0.0f, body, 0.9f, r);
    float lp = 0; for (size_t i = 0; i < b.size(); ++i) { float t = i / (float)SR; lp += (r.n() - lp) * bright; b[i] += lp * 2.5f * std::exp(-t * tail); }
    normalize(b); p->bufs[name] = b; };
  gun("shot_m1_garand", 4200, 70, 6.0f, 1.2f, 0.05f);
  gun("shot_m1c_garand", 4200, 70, 6.0f, 1.2f, 0.05f);
  gun("shot_kar98k", 3800, 62, 5.0f, 1.4f, 0.045f);
  gun("shot_kar98k_zf39", 3800, 62, 5.0f, 1.4f, 0.045f);
  gun("shot_mp40", 3000, 110, 14.0f, 0.5f, 0.09f);
  gun("shot_m1_thompson", 2400, 90, 12.0f, 0.55f, 0.07f);
  gun("shot_mg42", 5200, 85, 16.0f, 0.4f, 0.12f);
  gun("shot_m2hb", 3000, 45, 7.0f, 0.9f, 0.04f);
  { Buf b = mk(0.5f); addClang(b, 0.0f, 2600, 0.6f, 0.09f, r); addClang(b, 0.18f, 2400, 0.25f, 0.06f, r); normalize(b, 0.55f); p->bufs["ping"] = b; }
  // surface damage: glass shatter (bright clinks), splintering shutter, door torn off its hinges
  { Buf b = mk(0.7f); for (int k = 0; k < 9; ++k) addClang(b, 0.012f * k + 0.03f * (k % 3), 3200.0f + 380.0f * (k % 5), 0.35f, 0.05f, r); addClick(b, 0.0f, 0.05f, 5200, 0.7f, r); normalize(b, 0.55f); p->bufs["glass_break"] = b; }
  { Buf b = mk(0.6f); addClick(b, 0.0f, 0.03f, 1800, 0.8f, r); addThud(b, 0.02f, 160, 0.5f, r); addClick(b, 0.09f, 0.03f, 1300, 0.5f, r); normalize(b, 0.55f); p->bufs["shutter_break"] = b; }
  { Buf b = mk(0.9f); addClick(b, 0.0f, 0.04f, 1500, 0.8f, r); addThud(b, 0.03f, 90, 0.8f, r); addClang(b, 0.06f, 700, 0.2f, 0.15f, r); addThud(b, 0.45f, 70, 0.5f, r); normalize(b, 0.6f); p->bufs["door_break"] = b; }
  { Buf b = mk(2.2f); addClick(b, 0.0f, 0.05f, 1100, 0.9f, r); addClick(b, 0.12f, 0.06f, 800, 0.7f, r); addThud(b, 1.2f, 55, 1.0f, r); addThud(b, 1.35f, 70, 0.6f, r); normalize(b, 0.7f); p->bufs["tree_fall"] = b; }
  { Buf b = mk(2.5f); for (size_t k = 0; k < b.size(); ++k) { float t = k / 44100.0f; b[k] = 0.25f * std::sin(6.2831853f * 4100.0f * t) * std::exp(-t * 1.2f); } normalize(b, 0.25f); p->bufs["tinnitus"] = b; }
  { Buf b = mk(0.5f); addClick(b, 0.0f, 0.05f, 1600, 0.6f, r); addClick(b, 0.2f, 0.05f, 1900, 0.6f, r); normalize(b, 0.6f); p->bufs["bolt"] = b; }
  { Buf b = mk(0.3f); addClick(b, 0.0f, 0.06f, 1200, 0.7f, r); normalize(b, 0.5f); p->bufs["mag_out"] = b; }
  { Buf b = mk(0.3f); addClick(b, 0.0f, 0.04f, 2200, 0.8f, r); addThud(b, 0.01f, 160, 0.3f, r); normalize(b, 0.6f); p->bufs["mag_in"] = b; }
  { Buf b = mk(0.25f); addClick(b, 0.0f, 0.03f, 2800, 0.7f, r); normalize(b, 0.5f); p->bufs["clip_in"] = b; }
  p->bufs["rocket_in"] = p->bufs["clip_in"]; p->bufs["rocket_ready"] = p->bufs["bolt"];   // launcher loading: procedural (no bank foley)
  { Buf b = mk(0.8f); addClang(b, 0.0f, 900, 0.3f, 0.08f, r); addClick(b, 0.3f, 0.05f, 1500, 0.7f, r); addClang(b, 0.55f, 1100, 0.35f, 0.08f, r); normalize(b, 0.6f); p->bufs["belt"] = b; }
  // the synthesized weapon-handling sounds are off (user request): reloads, bolts, clicks play only where a recording exists
  for (const char* n : {"bolt", "mag_out", "mag_in", "clip_in", "rocket_in", "rocket_ready", "belt", "click", "ping", "breech"}) p->bufs.erase(n);
  // ambience loops (started once; never restarted on mode changes)
  p->windBuf = makeWind(14.0f, r, 0.02f, 1.0f);
  p->leafBuf = makeWind(9.0f, r, 0.35f, 2.0f);
  auto loop = [&](Buf& b, ma_audio_buffer& ab, ma_sound& s, float vol) {
    ma_audio_buffer_config c = ma_audio_buffer_config_init(ma_format_f32, 1, b.size(), b.data(), nullptr);
    c.sampleRate = SR;
    ma_audio_buffer_init(&c, &ab);
    ma_sound_init_from_data_source(&p->engine, &ab, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &s);
    p->route(s);
    ma_sound_set_looping(&s, MA_TRUE); ma_sound_set_volume(&s, vol); ma_sound_start(&s);
  };
  loop(p->windBuf, p->windAB, p->wind, 0.0f);
  loop(p->leafBuf, p->leafAB, p->leaves, 0.0f);
  ma_engine_set_volume(&p->engine, masterGain);
  ok_ = true;
  return true;
}

void Audio::shutdown() {
  if (!p) return;
  for (auto& v : p->voices) if (v.used) { ma_sound_uninit(&v.snd); ma_audio_buffer_uninit(&v.buf); }
  for (auto& v : p->loops) if (v.used) { ma_sound_uninit(&v.snd); ma_audio_buffer_uninit(&v.buf); }
  for (int k = 0; k < 2; ++k) if (p->bedOn[k]) { ma_sound_uninit(&p->bed[k]); ma_audio_buffer_uninit(&p->bedAB[k]); }
  ma_sound_uninit(&p->wind); ma_sound_uninit(&p->leaves);
  if (p->lpfOk) ma_lpf_node_uninit(&p->lpf, nullptr);
  ma_audio_buffer_uninit(&p->windAB); ma_audio_buffer_uninit(&p->leafAB);
  ma_engine_uninit(&p->engine);
  delete p; p = nullptr; ok_ = false;
}

// 16-bit PCM WAV -> mono float at the engine rate (channels averaged, linear resampling)
static bool loadWavMono(const fs::path& path, Buf& out) {
  std::ifstream f(path, std::ios::binary); if (!f) return false;
  std::vector<char> b((std::istreambuf_iterator<char>(f)), {});
  if (b.size() < 44 || std::memcmp(b.data(), "RIFF", 4) || std::memcmp(b.data() + 8, "WAVE", 4)) return false;
  int ch = 1, rate = SR, bits = 16; size_t p = 12; const int16_t* pcm = nullptr; size_t n = 0;
  while (p + 8 <= b.size()) {
    uint32_t sz; std::memcpy(&sz, b.data() + p + 4, 4);
    if (!std::memcmp(b.data() + p, "fmt ", 4)) { uint16_t c, bt; uint32_t r; std::memcpy(&c, b.data() + p + 10, 2); std::memcpy(&r, b.data() + p + 12, 4); std::memcpy(&bt, b.data() + p + 22, 2); ch = c; rate = (int)r; bits = bt; }
    if (!std::memcmp(b.data() + p, "data", 4)) { pcm = (const int16_t*)(b.data() + p + 8); n = std::min<size_t>(sz, b.size() - p - 8) / 2; }
    p += 8 + sz + (sz & 1);
  }
  if (!pcm || bits != 16 || ch < 1) return false;
  size_t frames = n / ch;
  std::vector<float> mono(frames);
  for (size_t i = 0; i < frames; ++i) { float s = 0; for (int c = 0; c < ch; ++c) s += pcm[i * ch + c]; mono[i] = s / (32768.0f * ch); }
  double step = (double)rate / SR; size_t outN = (size_t)(frames / step);
  out.resize(outN);
  for (size_t i = 0; i < outN; ++i) { double x = i * step; size_t a = (size_t)x; double t = x - a; out[i] = (float)(mono[std::min(a, frames - 1)] * (1 - t) + mono[std::min(a + 1, frames - 1)] * t); }
  return true;
}

int Audio::loadEvents(const fs::path& dir) {
  if (!p) return 0;
  json m; try { std::ifstream f(dir / "manifest.json"); m = json::parse(f); } catch (...) { logf("audio: no manifest in %s (procedural sounds only)", dir.string().c_str()); return 0; }
  for (auto& [name, e] : m["events"].items()) {
    Impl::Event ev; ev.gain = e.value("gain", 1.0f); ev.kind = e.value("kind", "");
    for (auto& v : e["variants"]) {
      std::string bank = v.value("bank", ""); for (const char* pre : {"ww2_weapon_", "ww2_", "cmn_"}) if (bank.rfind(pre, 0) == 0) { bank = bank.substr(std::strlen(pre)); break; }
      fs::path wav = dir / "samples" / (bank + "_" + std::to_string(v.value("index", 0)) + ".wav");
      Buf b; if (loadWavMono(wav, b) && !b.empty()) { ev.v.push_back(std::move(b)); ++samplesLoaded; } else ++samplesMissing;
    }
    if (!ev.v.empty()) { p->events[name] = std::move(ev); ++eventsLoaded; }
  }
  logf("audio: %d events from %d samples (%d missing: run tools/fsb5.py extract)", eventsLoaded, samplesLoaded, samplesMissing);
  return eventsLoaded;
}
// User-supplied recordings replace events: footsteps.mp3 (a walk: cut into single steps at its onsets) for every
// dry step, door_open.mp3 for doors / windows / shutters opening at half volume, zombie.mp3 as "zombie_groan".
int Audio::loadCustom(const fs::path& dir) {
  if (!p) return 0;
  auto decode = [&](const char* file, Buf& out) {
    ma_decoder_config dc = ma_decoder_config_init(ma_format_f32, 1, SR);
    ma_uint64 frames = 0; void* data = nullptr;
    if (ma_decode_file((dir / file).string().c_str(), &dc, &frames, &data) != MA_SUCCESS || !data) return false;
    out.assign((float*)data, (float*)data + frames); ma_free(data, nullptr);
    return !out.empty();
  };
  int used = 0; Buf b;
  struct StepSet { const char* file; std::vector<const char*> names; };   // fighter_steps.mp3: the Fighters' heavy tread (zombie.cpp)
  for (auto& ss : {StepSet{"footsteps.mp3", {"step", "step_stone", "step_wood"}}, StepSet{"grass_steps.mp3", {"step_grass"}}, StepSet{"fighter_steps.mp3", {"step_fighter"}}, StepSet{"spider_steps.mp3", {"step_spider"}}}) if (decode(ss.file, b)) {
    const int W = SR / 100; std::vector<float> env(b.size() / W, 0.0f); float peak = 0;   // 10 ms loudness windows
    for (size_t k = 0; k < env.size(); ++k) { float e = 0; for (int j = 0; j < W; ++j) e = std::max(e, std::fabs(b[k * W + j])); env[k] = e; peak = std::max(peak, e); }
    std::vector<size_t> on;   // onsets: loud after a quiet window, at least 0.25 s apart
    for (size_t k = 1; k < env.size(); ++k) if (env[k] > 0.3f * peak && env[k - 1] <= 0.3f * peak && (on.empty() || k - on.back() >= 25)) on.push_back(k);
    Impl::Event ev; ev.kind = "step";
    for (size_t i = 0; i < on.size() && ev.v.size() < 24; ++i) {
      size_t a = (on[i] > 1 ? on[i] - 1 : 0) * W, e = std::min({b.size(), a + (size_t)(0.45f * SR), i + 1 < on.size() ? (on[i + 1] - 1) * W : b.size()});
      if (e <= a + (size_t)W) continue;
      Buf s(b.begin() + a, b.begin() + e); float pk = 1e-4f; for (float x : s) pk = std::max(pk, std::fabs(x));
      size_t f = std::min(s.size(), (size_t)(0.03f * SR));
      for (size_t j = 0; j < s.size(); ++j) s[j] *= 0.5f / pk * (j + f >= s.size() ? (float)(s.size() - j) / f : 1.0f);   // even level, 30 ms fade-out
      ev.v.push_back(std::move(s));
    }
    if (!ev.v.empty()) { ++used; for (const char* n : ss.names) { p->events[n] = ev; if (std::string(n) == "step_fighter") p->events[n].minDist = 60.0f; } }
  }
  if (decode("door_open.mp3", b)) { ++used; Impl::Event ev; ev.v.push_back(b); ev.gain = 0.5f; for (const char* n : {"wood_open", "glass_open", "shutter_open", "steel_open"}) p->events[n] = ev; }
  // gun shots (guns/*.mp3): one recording per weapon group. A burst recording gives its last shot (the one with the tail);
  // automatic fire re-triggers it per round like every other shot event.
  struct GunSet { const char* file; bool burst; std::vector<const char*> ids; };
  const GunSet guns[] = {
    {"guns/pistol_a.mp3", false, {"luger"}},
    {"guns/pistol_c.mp3", false, {"modern_pistol2"}},
    {"guns/pistol_b.mp3", false, {"m1911a1"}},
    {"guns/pistol_9mm.mp3", false, {"modern_pistol", "modern_ar15", "modern_m16", "modern_m4", "modern_m4_irons", "modern_pistol3"}},
    {"guns/semi_rifle.mp3", false, {"modern_sniper_semi", "modern_ak", "modern_fal", "modern_scar", "fg42", "m1918a2_bar", "modern_ak12"}},
    {"guns/auto_rifle_burst.mp3", true, {"modern_mp7", "modern_scorpion"}},
    {"guns/carbine.mp3", false, {"m1_carbine"}},
    {"guns/rifle_bolt.mp3", false, {"kar98k", "m1_garand", "m1c_garand"}},
    {"guns/sniper_heavy.mp3", false, {"kar98k_zf39", "modern_sniper_bolt", "modern_sniper_50", "modern_sniper_3"}},
    {"guns/assault_rifle.mp3", true, {"stg44"}},
    {"guns/shotgun.mp3", false, {"modern_shotgun", "modern_shotgun2"}},
    {"guns/hmg_burst.mp3", true, {"m2hb_carry"}},
    {"guns/mg_burst.mp3", true, {"mg42"}},
    {"guns/mg2_burst.mp3", true, {"modern_lmg"}},
    {"guns/smg_burst.mp3", true, {"mp40", "m1_thompson", "m3_grease_gun", "modern_uzi"}},
  };
  for (auto& g : guns) {
    if (!decode(g.file, b)) continue;
    const int W = SR / 200; size_t nW = b.size() / W; float peak = 1e-4f; std::vector<float> env(nW, 0.0f);   // 5 ms windows
    for (size_t k = 0; k < nW; ++k) { for (int j = 0; j < W; ++j) env[k] = std::max(env[k], std::fabs(b[k * W + j])); peak = std::max(peak, env[k]); }
    size_t first = nW, last = nW, prev = 0;
    for (size_t k = 0; k < nW; ++k) if (env[k] > 0.5f * peak && (k == 0 || env[k - 1] <= 0.5f * peak) && (last == nW || k - prev >= 8)) { if (first == nW) first = k; last = prev = k; }
    if (first == nW) continue;
    size_t a = (g.burst ? last : first); a = (a > 0 ? a - 1 : 0) * W;
    size_t e = std::min(b.size(), a + (size_t)(2.5f * SR));
    Buf s(b.begin() + a, b.begin() + e); float pk = 1e-4f; for (float x : s) pk = std::max(pk, std::fabs(x));
    size_t f = std::min(s.size(), (size_t)(0.05f * SR));
    for (size_t j = 0; j < s.size(); ++j) s[j] *= 0.8f / pk * (j + f >= s.size() ? (float)(s.size() - j) / f : 1.0f);
    ++used; Impl::Event ev; ev.kind = "shot"; ev.v.push_back(std::move(s));
    for (const char* id : g.ids) { p->events[std::string("shot_") + id] = ev; p->events.erase(std::string("shot_") + id + "_far"); }
  }
  for (const char* id : {"shot_kar98k"}) if (auto it = p->events.find(id); it != p->events.end()) it->second.gain = 1.5f;   // louder (user request)
  // launchers, cannon and explosions: whole recordings from their first loud moment (up to 6 s), far layers dropped
  struct FxSet { const char* file; std::vector<const char*> events; float from = 0; };   // from: seconds into the recording to start at
  const FxSet fxs[] = {
    {"guns/rocket_launch.mp3", {"shot_bazooka_m9", "shot_panzerschreck", "shot_modern_rpg7", "shot_modern_glauncher"}, 3.0f},
    {"guns/launcher_fire.mp3", {"explosion_rocket"}},
    {"guns/cannon_fire.mp3", {"pak_fire", "flak_fire"}},
    {"guns/shell_impact.mp3", {"explosion", "explosion_big", "explosion_grenade"}},
  };
  for (auto& g : fxs) {
    if (!decode(g.file, b)) continue;
    if (g.from > 0) { size_t cut = (size_t)(g.from * SR); if (cut + (size_t)SR / 10 >= b.size()) continue; b.erase(b.begin(), b.begin() + cut); }
    float pk = 1e-4f; for (float x : b) pk = std::max(pk, std::fabs(x));
    size_t a = 0; while (g.from <= 0 && a < b.size() && std::fabs(b[a]) < 0.2f * pk) ++a;
    a = a > (size_t)(SR / 100) ? a - SR / 100 : 0;
    Buf s(b.begin() + a, b.begin() + std::min(b.size(), a + (size_t)(6.0f * SR)));
    size_t f = std::min(s.size(), (size_t)(0.2f * SR));
    for (size_t j = 0; j < s.size(); ++j) s[j] *= 0.9f / pk * (j + f >= s.size() ? (float)(s.size() - j) / f : 1.0f);
    ++used; Impl::Event ev; ev.v.push_back(std::move(s));
    for (const char* n : g.events) { p->events[n] = ev; p->events.erase(std::string(n) + "_far"); }
  }
  if (decode("headshot.mp3", b)) { ++used; float pk = 1e-4f; for (float x : b) pk = std::max(pk, std::fabs(x)); for (float& x : b) x *= 0.8f / pk; Impl::Event ev; ev.kind = "ui"; ev.v.push_back(b); p->events["ui_headshot"] = ev; }
  if (decode("fighter_laugh.mp3", b)) { ++used; Impl::Event ev; ev.v.push_back(b); p->events["fighter_laugh"] = ev; }
  if (decode("fighter_stomp.mp3", b)) { ++used; Impl::Event ev; ev.gain = 1.5f; ev.minDist = 60.0f; ev.v.push_back(b); p->events["fighter_stomp"] = ev; }
  { Impl::Event ev; for (const char* f : {"spider_attack_1.mp3", "spider_attack_2.mp3", "spider_attack_3.mp3"}) if (decode(f, b)) ev.v.push_back(b);   // the spider's strike: a random one each time
    if (!ev.v.empty()) { ++used; p->events["spider_attack"] = ev; } }
  for (const char* n : {"troll_attack", "troll_idle"}) if (decode((std::string(n) + ".mp3").c_str(), b)) { ++used; if (b.size() > (size_t)(5 * SR)) { b.resize(5 * SR); for (int j = 0; j < SR / 5; ++j) b[b.size() - 1 - j] *= j / (SR / 5.0f); } Impl::Event ev; ev.v.push_back(b); p->events[n] = ev; }
  {   // reload recordings (reload/*.mp3): one whole reload per magazine / clip / belt weapon at reload start; shotguns: the pump after each
      // shot and one mid-track shell-loading sound per shell (also the single-round loading sound of other guns)
    // SND3: secs: the group's shortest (partial) reload time; the recording is fitted to it (fitTo); keep: seconds of the
    // file used (the AK recording's loud second half is dropped)
    struct RSet { const char* file; std::vector<const char*> ids; float secs = 6.0f, keep = 0; };
    const RSet rs[] = {
      {"reload/reload_rifle.mp3", {"mg42", "m2hb_carry"}},   // belt guns keep the generic rifle recording
      {"reload/reload_ak.mp3", {"modern_ak", "modern_ak12"}, 2.4f, 3.5f},   // rock-in magazines (jigokukarano_sisya)
      {"reload/reload_ak.mp3", {"stg44"}, 2.7f, 3.5f},
      {"reload/reload_m4.mp3", {"modern_m4", "modern_m4_irons"}, 2.2f},   // AR pattern and the other magazine rifles (squarebun)
      {"reload/reload_m4.mp3", {"modern_m16", "modern_scar"}, 2.3f},
      {"reload/reload_m4.mp3", {"modern_ar15", "modern_fal"}, 2.4f},
      {"reload/reload_m4.mp3", {"modern_sniper_semi"}, 2.5f},
      {"reload/reload_m4.mp3", {"fg42"}, 2.7f},
      {"reload/reload_m4.mp3", {"m1918a2_bar"}, 3.0f},
      {"reload/reload_m4.mp3", {"m1_carbine"}, 2.0f},
      {"reload/reload_m4.mp3", {"modern_lmg"}, 5.2f},   // the M249's box change (nothing better)
      {"reload/reload_sniper.mp3", {"modern_sniper_bolt", "modern_sniper_3"}, 2.8f},   // bolt magazine snipers (the user's "m24 and 50cal")
      {"reload/reload_sniper.mp3", {"modern_sniper_50"}, 4.2f},
      {"reload/reload_bolt.mp3", {"kar98k"}, 2.2f},   // the stripper-clip reload (freesound_community "rifle or shotgun reload")
      {"reload/reload_a.mp3", {"mp40", "m1_thompson", "m3_grease_gun", "modern_uzi", "modern_mp7", "modern_scorpion", "m1_garand", "m1c_garand", "kar98k_zf39"}},
      {"reload/reload_b.mp3", {"luger", "m1911a1", "modern_pistol", "modern_pistol2", "modern_pistol3"}},
    };
    auto norm = [&](Buf& s, float secs) { if (s.size() > (size_t)(secs * SR)) s.resize((size_t)(secs * SR)); float pk = 1e-4f; for (float x : s) pk = std::max(pk, std::fabs(x)); size_t f = std::min(s.size(), (size_t)(0.05f * SR)); for (size_t j = 0; j < s.size(); ++j) s[j] *= 0.7f / pk * (j + f >= s.size() ? (float)(s.size() - j) / f : 1.0f); };
    // SND3: a reload longer than the weapon's reload is shortened in its quiet gaps (each kept at least 0.1 s, 20 ms equal-power
    // joins), so the whole mag-out / mag-in / charge sequence stays, at its own pitch; still too long, norm fades it out at secs
    auto fitTo = [&](Buf& s, float secs) {
      const size_t want = (size_t)(secs * SR), X = SR / 50, keepGap = SR / 10;
      if (s.size() <= want) return;
      float pk = 1e-4f; for (float x : s) pk = std::max(pk, std::fabs(x));
      std::vector<std::pair<size_t, size_t>> gaps; size_t g0 = SIZE_MAX, last = 0;   // quiet runs (20 ms peak under 12 % of the loudest) over 0.2 s, before the last sound
      for (size_t k = 0; k + X <= s.size(); k += X) { float e = 0; for (size_t j = 0; j < X; ++j) e = std::max(e, std::fabs(s[k + j])); if (e >= 0.12f * pk) last = k + X;
        bool q = e < 0.12f * pk; if (q && g0 == SIZE_MAX) g0 = k; if (!q && g0 != SIZE_MAX) { if (k - g0 > (size_t)(0.2f * SR)) gaps.push_back({g0, k}); g0 = SIZE_MAX; } }
      size_t content = std::min(s.size(), last + SR / 10);   // the trailing quiet beyond the last sound goes anyway
      size_t spare = 0; for (auto& g : gaps) spare += g.second - g.first - keepGap;
      float frac = content > want && spare ? std::min(1.0f, (float)(content - want) / spare) : 0.0f;
      Buf out; size_t pos = 0;
      for (auto& g : gaps) {
        size_t cut = (size_t)((g.second - g.first - keepGap) * frac), a = g.first + (g.second - g.first - cut) / 2;
        if (cut < 2 * X || a < pos + X || a + cut + X > s.size()) continue;
        out.insert(out.end(), s.begin() + pos, s.begin() + a);
        size_t o = out.size() - X; for (size_t j = 0; j < X; ++j) { float t = (j + 0.5f) / X * 1.5707963f; out[o + j] = out[o + j] * std::cos(t) + s[a + cut + j] * std::sin(t); }
        pos = a + cut + X;
      }
      out.insert(out.end(), s.begin() + pos, s.begin() + content); s.swap(out);
    };
    for (auto& g : rs) if (decode(g.file, b)) {
      if (g.keep > 0 && b.size() > (size_t)(g.keep * SR)) { b.resize((size_t)(g.keep * SR)); size_t f = SR / 20; for (size_t j = 0; j < f; ++j) b[b.size() - 1 - j] *= (float)j / f; }
      ++used; fitTo(b, g.secs); norm(b, g.secs);
      { static const bool lg = std::getenv("NMK_AUDIO_LOG") != nullptr; if (lg) logf("audio: %s for %s: %.2f s (fit to %.1f s)", g.file, g.ids[0], b.size() / (float)SR, g.secs); }   // diagnostics
      Impl::Event ev; ev.v.push_back(b); for (const char* id : g.ids) p->events[std::string("reload_") + id] = ev;
    }
    if (decode("reload/shotgun_pump.mp3", b)) { ++used; norm(b, 1.5f); b.insert(b.begin(), (size_t)(0.3f * SR), 0.0f); Impl::Event ev; ev.v.push_back(b); p->events["shotgun_pump"] = ev; }
    if (decode("reload/shotgun_shells.mp3", b)) {
      const int W = SR / 200; size_t nW = b.size() / W; float peak = 1e-4f; std::vector<float> env(nW, 0.0f);
      for (size_t k = 0; k < nW; ++k) { for (int j = 0; j < W; ++j) env[k] = std::max(env[k], std::fabs(b[k * W + j])); peak = std::max(peak, env[k]); }
      std::vector<size_t> on; for (size_t k = 1; k < nW; ++k) if (env[k] > 0.35f * peak && env[k - 1] <= 0.35f * peak && (on.empty() || k - on.back() >= 30)) on.push_back(k);
      if (!on.empty()) {
        size_t m = on.size() / 2, a = (on[m] > 2 ? on[m] - 2 : 0) * W, e = std::min({b.size(), a + (size_t)(0.5f * SR), m + 1 < on.size() ? (on[m + 1] - 2) * W : b.size()});
        if (e > a + (size_t)W) { Buf s(b.begin() + a, b.begin() + e); norm(s, 0.5f); ++used; Impl::Event ev; ev.v.push_back(std::move(s)); p->events["clip_in"] = ev; }
      }
    }
  }
  if (decode("zombie.mp3", b)) { ++used; Impl::Event ev; ev.gain = 1.8f;   /* louder (user request) */ ev.v.push_back(b); p->events["zombie_groan"] = ev; }
  auto peakOf = [](const Buf& s) { float pk = 1e-4f; for (float x : s) pk = std::max(pk, std::fabs(x)); return pk; };
  // SND2: the creature's strike: the strongest 1.25 s of the shriek (by 10 ms loudness), short fades, levelled
  if (decode("creature_shriek.mp3", b)) {
    const int W = SR / 100; size_t nW = b.size() / W; std::vector<float> e(nW, 0.0f);
    for (size_t k = 0; k < nW; ++k) { for (int j = 0; j < W; ++j) e[k] += b[k * W + j] * b[k * W + j]; }
    size_t len = std::min(nW, (size_t)125), best = 0; float bs = -1, run = 0;
    for (size_t k = 0; k < nW; ++k) { run += e[k]; if (k >= len) run -= e[k - len]; if (k + 1 >= len && run > bs) { bs = run; best = k + 1 - len; } }
    Buf s(b.begin() + best * W, b.begin() + std::min(b.size(), (best + len) * W)); float pk = peakOf(s);
    size_t fi = SR / 100, fo = SR * 8 / 100;
    for (size_t j = 0; j < s.size(); ++j) s[j] *= 0.9f / pk * std::min({1.0f, (float)j / fi, (float)(s.size() - j) / fo});
    ++used; Impl::Event ev; ev.minDist = 4.0f; ev.v.push_back(std::move(s)); p->events["creature_shriek"] = ev;
  }
  // SND2: the creature's ambience: only the loud stretches of the recording (50 ms loudness above a quarter of its loudest,
  // gaps under 0.15 s bridged, stretches under 0.4 s dropped), joined with 120 ms equal-power cross-fades and closed into
  // a loop by one more cross-fade from its end into its start, so neither the joins nor the loop point click or gap
  if (decode("creature_ambience.mp3", b)) {
    const int W = SR / 20; size_t nW = b.size() / W; std::vector<float> rms(nW, 0.0f); float top = 1e-6f;
    for (size_t k = 0; k < nW; ++k) { double q = 0; for (int j = 0; j < W; ++j) q += b[k * W + j] * b[k * W + j]; rms[k] = (float)std::sqrt(q / W); top = std::max(top, rms[k]); }
    std::vector<std::pair<size_t, size_t>> runs;   // [first, last] windows
    for (size_t k = 0; k < nW; ++k) if (rms[k] > 0.25f * top) { if (!runs.empty() && k - runs.back().second <= 3) runs.back().second = k; else runs.push_back({k, k}); }
    const size_t X = (size_t)(0.12f * SR); Buf loop;
    auto eqp = [&](Buf& out, const Buf& in) {   // out's tail and in's head overlap by X: cos / sin gains, so the power stays even
      size_t x = std::min({X, out.size(), in.size()}), o = out.size() - x;
      for (size_t j = 0; j < x; ++j) { float t = (j + 0.5f) / x * 1.5707963f; out[o + j] = out[o + j] * std::cos(t) + in[j] * std::sin(t); }
      out.insert(out.end(), in.begin() + x, in.end());
    };
    int kept = 0;
    for (auto& r : runs) {
      if ((r.second - r.first + 1) * W < (size_t)(0.3f * SR)) continue;
      size_t a = r.first * W > X / 2 ? r.first * W - X / 2 : 0, z = std::min(b.size(), (r.second + 1) * W + X / 2);
      Buf s(b.begin() + a, b.begin() + z); if (loop.empty()) loop = s; else eqp(loop, s); ++kept;
    }
    if (loop.size() > 3 * X) {
      for (size_t j = 0; j < X; ++j) { float t = (j + 0.5f) / X * 1.5707963f; loop[j] = loop[j] * std::sin(t) + loop[loop.size() - X + j] * std::cos(t); }   // the loop point
      loop.resize(loop.size() - X);
      float pk = peakOf(loop); for (float& x : loop) x *= 0.8f / pk;
      logf("audio: creature_ambience loop %.1f s from %d loud stretches of the %.1f s recording", loop.size() / (float)SR, kept, b.size() / (float)SR);
      if (std::getenv("NMK_AUDIO_DUMP")) { FILE* f = std::fopen((exeDir() / "selftest_out" / "creature_ambience_loop.f32").string().c_str(), "wb"); if (f) { std::fwrite(loop.data(), 4, loop.size(), f); std::fclose(f); } }   // diagnostics: the built loop, raw 44.1 kHz mono float
      ++used; addSound("creature_ambience", std::move(loop));
    }
  }
  // SND1: any other mp3 here plays as an event named after its file (knightattack.mp3 -> "knightattack"): leading
  // silence trimmed, levelled, at most 6 s. A type's "sounds" (content/ai/zombie*.json) name them.
  std::error_code ec;
  for (auto& f : fs::directory_iterator(dir, ec)) {
    if (!f.is_regular_file() || f.path().extension() != ".mp3") continue;
    std::string stem = f.path().stem().string();
    if (p->events.count(stem) || p->bufs.count(stem) || stem.rfind("creature_", 0) == 0) continue;
    if (!decode(f.path().filename().string().c_str(), b)) continue;
    float pk = peakOf(b); size_t a = 0; while (a < b.size() && std::fabs(b[a]) < 0.03f * pk) ++a;
    a = a > (size_t)(SR / 200) ? a - SR / 200 : 0;
    Buf s(b.begin() + a, b.begin() + std::min(b.size(), a + (size_t)(6 * SR))); size_t fo = std::min(s.size(), (size_t)(SR / 20));
    for (size_t j = 0; j < s.size(); ++j) s[j] *= 0.8f / pk * (j + fo >= s.size() ? (float)(s.size() - j) / fo : 1.0f);
    ++used; Impl::Event ev; ev.v.push_back(std::move(s)); p->events[stem] = ev;
  }
  return used;
}
bool Audio::hasEvent(const std::string& name) const { return p && p->events.count(name); }
int Audio::eventVariant(const std::string& name) const { if (!p) return -1; auto it = p->events.find(name); return it == p->events.end() ? -1 : it->second.last; }

void Audio::play(const std::string& name, vec3 pos, float volume, float pitch) {
  ++playCount; lastPlayed = name; lastPos = pos;
  if (!p) return;
  // resolve: the event (its far layer beyond 90 m), else the procedural buffer, else the name without its last
  // "_suffix" (explosion_grenade -> explosion), so a missing sample never silences an event
  const Buf* src = nullptr; std::string used; float gain = 1, minDist = 1; bool shot = false, step = false, uiEv = false;
  std::string n = name;
  for (int tries = 0; tries < 3 && !src; ++tries) {
    std::string cand = n;
    if (glm::length(pos - p->listener) > 90.0f && p->events.count(n + "_far")) cand = n + "_far";
    auto e = p->events.find(cand);
    if (e != p->events.end()) {
      auto& ev = e->second; int k = 0;
      if (ev.v.size() > 1) { k = (int)(p->pick() % (ev.v.size() - 1)); if (ev.last >= 0 && k >= ev.last) ++k; }   // never the same twice running
      ev.last = k; src = &ev.v[k]; used = cand; gain = ev.gain; minDist = ev.minDist; uiEv = ev.kind == "ui"; shot = ev.kind == "shot"; step = ev.kind == "step";
    } else if (auto b = p->bufs.find(n); b != p->bufs.end()) { src = &b->second; used = n; }
    else { size_t u = n.rfind('_'); if (u == std::string::npos) break; n = n.substr(0, u); }
  }
  if (!src) return;
  lastEvent = used; ++eventPlays[used];
  { static const bool lg = std::getenv("NMK_AUDIO_LOG") != nullptr; if (lg) logf("audio play %s vol %.2f pitch %.2f at %.1f m", used.c_str(), volume, pitch, glm::length(pos - p->listener)); }   // diagnostics
  double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  if (shot)   // automatic fire: the previous body of this gun yields to the new shot (no stacking / clipping)
    for (auto& c : p->voices) if (c.used && c.event == used && !ma_sound_at_end(&c.snd) && now - c.start < 0.35) {
      ma_sound_set_fade_in_milliseconds(&c.snd, -1, 0, 40);
      ma_sound_set_stop_time_in_milliseconds(&c.snd, ma_engine_get_time_in_milliseconds(&p->engine) + 45);
    }
  if (step)   // long recorded strides (wading): the previous one's water tail yields to the next stride over 250 ms
    for (auto& c : p->voices) if (c.used && c.event == used && !ma_sound_at_end(&c.snd)) {
      ma_sound_set_fade_in_milliseconds(&c.snd, -1, 0, 250);
      ma_sound_set_stop_time_in_milliseconds(&c.snd, ma_engine_get_time_in_milliseconds(&p->engine) + 260);
    }
  Impl::Voice* v = nullptr;
  for (auto& c : p->voices) if (!c.used || ma_sound_at_end(&c.snd)) { v = &c; break; }
  if (!v) { for (auto& c : p->voices) if (!v || c.start < v->start) v = &c; }   // all busy: steal the oldest
  if (v->used) { ma_sound_uninit(&v->snd); ma_audio_buffer_uninit(&v->buf); }
  volume *= gain * ((uiEv || name.rfind("ui_", 0) == 0) ? uiGain : effectsGain);
  ma_audio_buffer_config c = ma_audio_buffer_config_init(ma_format_f32, 1, src->size(), src->data(), nullptr);
  c.sampleRate = SR;
  ma_audio_buffer_init(&c, &v->buf);
  ma_sound_init_from_data_source(&p->engine, &v->buf, 0, nullptr, &v->snd);
  p->route(v->snd);
  ma_sound_set_position(&v->snd, pos.x, pos.y, pos.z);
  ma_sound_set_min_distance(&v->snd, minDist);
  ma_sound_set_rolloff(&v->snd, 1.2f);
  ma_sound_set_volume(&v->snd, volume);
  if (pitch != 1.0f) ma_sound_set_pitch(&v->snd, std::max(0.05f, pitch));
  ma_sound_start(&v->snd);
  v->used = true; v->event = used; v->start = now;
}

void Audio::setListener(vec3 pos, vec3 fwd) {
  if (!p) return;
  p->listener = pos;
  ma_engine_listener_set_position(&p->engine, 0, pos.x, pos.y, pos.z);
  ma_engine_listener_set_direction(&p->engine, 0, fwd.x, fwd.y, fwd.z);
}
void Audio::setAmbience(float wind, float leaves) {
  if (!p) return;
  p->windVol = wind; p->leafVol = leaves;
  ma_sound_set_volume(&p->wind, wind * ambienceGain);
  ma_sound_set_volume(&p->leaves, leaves * ambienceGain);
}
void Audio::setBed(int slot, const fs::path& file, float volume) {
  if (!p || slot < 0 || slot > 1) return;
  std::string f = file.string();
  if (f != p->bedFile[slot]) {
    if (p->bedOn[slot]) { ma_sound_uninit(&p->bed[slot]); ma_audio_buffer_uninit(&p->bedAB[slot]); p->bedOn[slot] = false; }
    p->bedFile[slot] = f;
    // decoded whole (stereo) so the ends can be cross-faded: a streamed mp3 loop clicks and gaps at the seam
    ma_decoder_config dc = ma_decoder_config_init(ma_format_f32, 2, SR);
    ma_uint64 frames = 0; void* data = nullptr;
    if (!f.empty() && ma_decode_file(f.c_str(), &dc, &frames, &data) == MA_SUCCESS && data) {
      Buf& b = p->bedBuf[slot]; b.assign((float*)data, (float*)data + frames * 2); ma_free(data, nullptr);
      size_t n = (size_t)frames, a = 0, e = n;   // trim the encoder silence at both ends, then blend the last 3 s into the first 3 s
      auto loud = [&](size_t k) { return std::fabs(b[2 * k]) + std::fabs(b[2 * k + 1]) > 0.004f; };
      while (a < e && !loud(a)) ++a;
      while (e > a && !loud(e - 1)) --e;
      size_t xf = std::min((size_t)(3 * SR), (e - a) / 4);
      if (e - a > (size_t)SR && xf > 0) {
        for (size_t k = 0; k < xf; ++k) { float w = k / (float)xf, g0 = std::sqrt(w), g1 = std::sqrt(1 - w);   // equal power
          for (int c = 0; c < 2; ++c) b[2 * (a + k) + c] = b[2 * (a + k) + c] * g0 + b[2 * (e - xf + k) + c] * g1; }
        b.erase(b.begin() + 2 * (e - xf), b.end()); b.erase(b.begin(), b.begin() + 2 * a);
        ma_audio_buffer_config c = ma_audio_buffer_config_init(ma_format_f32, 2, b.size() / 2, b.data(), nullptr);
        c.sampleRate = SR;
        if (ma_audio_buffer_init(&c, &p->bedAB[slot]) == MA_SUCCESS) {
          if (ma_sound_init_from_data_source(&p->engine, &p->bedAB[slot], MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &p->bed[slot]) == MA_SUCCESS) {
            p->bedOn[slot] = true; p->route(p->bed[slot]); ma_sound_set_looping(&p->bed[slot], MA_TRUE); ma_sound_start(&p->bed[slot]);
          } else ma_audio_buffer_uninit(&p->bedAB[slot]);
        }
      }
    }
  }
  p->bedVol[slot] = volume;
  if (p->bedOn[slot]) ma_sound_set_volume(&p->bed[slot], volume * ambienceGain);
}
void Audio::setUnderwater(float k) {
  k = std::clamp(k, 0.0f, 1.0f);
  underwater = k;
  if (!p || !p->lpfOk) return;
  float cut = k > 0 ? 20000.0f * std::pow(550.0f / 20000.0f, k) : 20000.0f;   // 20 kHz -> 550 Hz (log blend)
  if (std::fabs(cut - p->lpfCut) < 1.0f) return;
  p->lpfCut = cut;
  ma_lpf_config c = ma_lpf_config_init(ma_format_f32, ma_engine_get_channels(&p->engine), ma_engine_get_sample_rate(&p->engine), cut, 2);
  ma_lpf_node_reinit(&c, &p->lpf);
  ma_node_set_output_bus_volume(&p->lpf, 0, 1.0f - 0.35f * k);
}

void Audio::setGains(float master, float effects, float ui, float ambience) {
  masterGain = std::clamp(master, 0.0f, 1.0f); effectsGain = std::clamp(effects, 0.0f, 1.0f);
  uiGain = std::clamp(ui, 0.0f, 1.0f); ambienceGain = std::clamp(ambience, 0.0f, 1.0f);
  if (!p) return;
  ma_engine_set_volume(&p->engine, masterGain);
  ma_sound_set_volume(&p->wind, p->windVol * ambienceGain);
  ma_sound_set_volume(&p->leaves, p->leafVol * ambienceGain);
  for (int k = 0; k < 2; ++k) if (p->bedOn[k]) ma_sound_set_volume(&p->bed[k], p->bedVol[k] * ambienceGain);
}

// ---- looping voices (tanks): one sound per handle, pitch / volume / position updated every step, never restarted
void Audio::addSound(const std::string& name, std::vector<float> b) { if (p) p->bufs[name] = std::move(b); }
int Audio::loopStart(const std::string& name, vec3 pos, float volume, float pitch, float offset) {
  if (!p) return -1;
  auto it = p->bufs.find(name);
  if (it == p->bufs.end()) return -1;
  for (int k = 0; k < 16; ++k) {
    auto& v = p->loops[k];
    if (v.used) continue;
    ma_audio_buffer_config c = ma_audio_buffer_config_init(ma_format_f32, 1, it->second.size(), it->second.data(), nullptr);
    c.sampleRate = SR;
    ma_audio_buffer_init(&c, &v.buf);
    ma_sound_init_from_data_source(&p->engine, &v.buf, 0, nullptr, &v.snd);
    p->route(v.snd);
    ma_sound_set_looping(&v.snd, MA_TRUE);
    ma_sound_set_min_distance(&v.snd, 3.0f); ma_sound_set_rolloff(&v.snd, 1.0f);
    ma_sound_set_position(&v.snd, pos.x, pos.y, pos.z); ma_sound_set_volume(&v.snd, volume * effectsGain); ma_sound_set_pitch(&v.snd, std::max(0.05f, pitch));
    if (offset > 0) ma_sound_seek_to_pcm_frame(&v.snd, (ma_uint64)(std::fmod(offset, 1.0f) * it->second.size()));   // SND2: a pack does not breathe in unison
    ma_sound_start(&v.snd);
    v.used = true;
    return k;
  }
  return -1;
}
void Audio::loopSet(int h, vec3 pos, float volume, float pitch) {
  if (!p || h < 0 || h >= 16 || !p->loops[h].used) return;
  auto& v = p->loops[h];
  ma_sound_set_position(&v.snd, pos.x, pos.y, pos.z); ma_sound_set_volume(&v.snd, volume * effectsGain); ma_sound_set_pitch(&v.snd, std::max(0.05f, pitch));
}
void Audio::loopStop(int h) {
  if (!p || h < 0 || h >= 16 || !p->loops[h].used) return;
  auto& v = p->loops[h];
  ma_sound_stop(&v.snd); ma_sound_uninit(&v.snd); ma_audio_buffer_uninit(&v.buf); v.used = false;
}
int Audio::loopsActive() const { int n = 0; if (p) for (auto& v : p->loops) n += v.used; return n; }
