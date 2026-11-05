#include "audio.h"
#include <miniaudio.h>
#include <random>
#include <cmath>

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
  struct Voice { ma_audio_buffer buf; ma_sound snd; bool used = false; };
  Voice voices[24];
  Buf windBuf, leafBuf;
  ma_audio_buffer windAB, leafAB; ma_sound wind, leaves;
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
  { Buf b = mk(0.5f); addClick(b, 0.0f, 0.05f, 1600, 0.6f, r); addClick(b, 0.2f, 0.05f, 1900, 0.6f, r); normalize(b, 0.6f); p->bufs["bolt"] = b; }
  { Buf b = mk(0.3f); addClick(b, 0.0f, 0.06f, 1200, 0.7f, r); normalize(b, 0.5f); p->bufs["mag_out"] = b; }
  { Buf b = mk(0.3f); addClick(b, 0.0f, 0.04f, 2200, 0.8f, r); addThud(b, 0.01f, 160, 0.3f, r); normalize(b, 0.6f); p->bufs["mag_in"] = b; }
  { Buf b = mk(0.25f); addClick(b, 0.0f, 0.03f, 2800, 0.7f, r); normalize(b, 0.5f); p->bufs["clip_in"] = b; }
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
  loop(p->windBuf, p->windAB, p->wind, 0.25f);
  loop(p->leafBuf, p->leafAB, p->leaves, 0.0f);
  ok_ = true;
  return true;
}

void Audio::shutdown() {
  if (!p) return;
  for (auto& v : p->voices) if (v.used) { ma_sound_uninit(&v.snd); ma_audio_buffer_uninit(&v.buf); }
  ma_sound_uninit(&p->wind); ma_sound_uninit(&p->leaves);
  ma_audio_buffer_uninit(&p->windAB); ma_audio_buffer_uninit(&p->leafAB);
  ma_engine_uninit(&p->engine);
  delete p; p = nullptr; ok_ = false;
}

void Audio::play(const std::string& name, vec3 pos, float volume) {
  ++playCount; lastPlayed = name; lastPos = pos;
  if (!p) return;
  auto it = p->bufs.find(name);
  if (it == p->bufs.end()) return;
  Impl::Voice* v = nullptr;
  for (auto& c : p->voices) if (!c.used || ma_sound_at_end(&c.snd)) { v = &c; break; }
  if (!v) return;
  if (v->used) { ma_sound_uninit(&v->snd); ma_audio_buffer_uninit(&v->buf); }
  ma_audio_buffer_config c = ma_audio_buffer_config_init(ma_format_f32, 1, it->second.size(), it->second.data(), nullptr);
  c.sampleRate = SR;
  ma_audio_buffer_init(&c, &v->buf);
  ma_sound_init_from_data_source(&p->engine, &v->buf, 0, nullptr, &v->snd);
  ma_sound_set_position(&v->snd, pos.x, pos.y, pos.z);
  ma_sound_set_min_distance(&v->snd, 1.0f);
  ma_sound_set_rolloff(&v->snd, 1.2f);
  ma_sound_set_volume(&v->snd, volume);
  ma_sound_start(&v->snd);
  v->used = true;
}

void Audio::setListener(vec3 pos, vec3 fwd) {
  if (!p) return;
  ma_engine_listener_set_position(&p->engine, 0, pos.x, pos.y, pos.z);
  ma_engine_listener_set_direction(&p->engine, 0, fwd.x, fwd.y, fwd.z);
}
void Audio::setAmbience(float wind, float leaves) {
  if (!p) return;
  ma_sound_set_volume(&p->wind, wind);
  ma_sound_set_volume(&p->leaves, leaves);
}
