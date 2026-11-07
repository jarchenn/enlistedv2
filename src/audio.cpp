#include "audio.h"
#include <cstring>
#include <miniaudio.h>
#include <random>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <chrono>

namespace {
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
  struct Event { std::vector<Buf> v; float gain = 1; std::string kind; int last = -1; };
  std::map<std::string, Event> events;
  std::mt19937 pick{20260606};
  vec3 listener{0};
  struct Voice { ma_audio_buffer buf; ma_sound snd; bool used = false; std::string event; double start = 0; };
  Voice voices[48];
  Buf windBuf, leafBuf; float windVol = 0.25f, leafVol = 0.0f;
  ma_audio_buffer windAB, leafAB; ma_sound wind, leaves;
  Voice loops[16];
};

bool Audio::init() {
  p = new Impl();
  ma_engine_config cfg = ma_engine_config_init();
  cfg.sampleRate = SR;
  if (ma_engine_init(&cfg, &p->engine) != MA_SUCCESS) { logf("audio: no device, sound disabled"); delete p; p = nullptr; return false; }
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
  // ambience loops (started once; never restarted on mode changes)
  p->windBuf = makeWind(14.0f, r, 0.02f, 1.0f);
  p->leafBuf = makeWind(9.0f, r, 0.35f, 2.0f);
  auto loop = [&](Buf& b, ma_audio_buffer& ab, ma_sound& s, float vol) {
    ma_audio_buffer_config c = ma_audio_buffer_config_init(ma_format_f32, 1, b.size(), b.data(), nullptr);
    c.sampleRate = SR;
    ma_audio_buffer_init(&c, &ab);
    ma_sound_init_from_data_source(&p->engine, &ab, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &s);
    ma_sound_set_looping(&s, MA_TRUE); ma_sound_set_volume(&s, vol); ma_sound_start(&s);
  };
  loop(p->windBuf, p->windAB, p->wind, 0.25f * ambienceGain);
  loop(p->leafBuf, p->leafAB, p->leaves, 0.0f);
  ma_engine_set_volume(&p->engine, masterGain);
  ok_ = true;
  return true;
}

void Audio::shutdown() {
  if (!p) return;
  for (auto& v : p->voices) if (v.used) { ma_sound_uninit(&v.snd); ma_audio_buffer_uninit(&v.buf); }
  for (auto& v : p->loops) if (v.used) { ma_sound_uninit(&v.snd); ma_audio_buffer_uninit(&v.buf); }
  ma_sound_uninit(&p->wind); ma_sound_uninit(&p->leaves);
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
bool Audio::hasEvent(const std::string& name) const { return p && p->events.count(name); }
int Audio::eventVariant(const std::string& name) const { if (!p) return -1; auto it = p->events.find(name); return it == p->events.end() ? -1 : it->second.last; }

void Audio::play(const std::string& name, vec3 pos, float volume) {
  ++playCount; lastPlayed = name; lastPos = pos;
  if (!p) return;
  // resolve: the event (its far layer beyond 90 m), else the procedural buffer, else the name without its last
  // "_suffix" (explosion_grenade -> explosion), so a missing sample never silences an event
  const Buf* src = nullptr; std::string used; float gain = 1; bool shot = false, step = false, uiEv = false;
  std::string n = name;
  for (int tries = 0; tries < 3 && !src; ++tries) {
    std::string cand = n;
    if (glm::length(pos - p->listener) > 90.0f && p->events.count(n + "_far")) cand = n + "_far";
    auto e = p->events.find(cand);
    if (e != p->events.end()) {
      auto& ev = e->second; int k = 0;
      if (ev.v.size() > 1) { k = (int)(p->pick() % (ev.v.size() - 1)); if (ev.last >= 0 && k >= ev.last) ++k; }   // never the same twice running
      ev.last = k; src = &ev.v[k]; used = cand; gain = ev.gain; uiEv = ev.kind == "ui"; shot = ev.kind == "shot"; step = ev.kind == "step";
    } else if (auto b = p->bufs.find(n); b != p->bufs.end()) { src = &b->second; used = n; }
    else { size_t u = n.rfind('_'); if (u == std::string::npos) break; n = n.substr(0, u); }
  }
  if (!src) return;
  lastEvent = used; ++eventPlays[used];
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
  ma_sound_set_position(&v->snd, pos.x, pos.y, pos.z);
  ma_sound_set_min_distance(&v->snd, 1.0f);
  ma_sound_set_rolloff(&v->snd, 1.2f);
  ma_sound_set_volume(&v->snd, volume);
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
void Audio::setGains(float master, float effects, float ui, float ambience) {
  masterGain = std::clamp(master, 0.0f, 1.0f); effectsGain = std::clamp(effects, 0.0f, 1.0f);
  uiGain = std::clamp(ui, 0.0f, 1.0f); ambienceGain = std::clamp(ambience, 0.0f, 1.0f);
  if (!p) return;
  ma_engine_set_volume(&p->engine, masterGain);
  ma_sound_set_volume(&p->wind, p->windVol * ambienceGain);
  ma_sound_set_volume(&p->leaves, p->leafVol * ambienceGain);
}

// ---- looping voices (tanks): one sound per handle, pitch / volume / position updated every step, never restarted
void Audio::addSound(const std::string& name, std::vector<float> b) { if (p) p->bufs[name] = std::move(b); }
int Audio::loopStart(const std::string& name, vec3 pos, float volume, float pitch) {
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
    ma_sound_set_looping(&v.snd, MA_TRUE);
    ma_sound_set_min_distance(&v.snd, 3.0f); ma_sound_set_rolloff(&v.snd, 1.0f);
    ma_sound_set_position(&v.snd, pos.x, pos.y, pos.z); ma_sound_set_volume(&v.snd, volume * effectsGain); ma_sound_set_pitch(&v.snd, std::max(0.05f, pitch));
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
