#include <atomic>
#include <thread>
#include "audio.h"
#include <cstring>
#include <miniaudio.h>
#include <random>
#include <cmath>
#include <map>
#include <deque>
#include <set>
#include <algorithm>
#include <fstream>
#include <memory>
#include <chrono>

namespace {
#include <cstdio>
#include <cstdlib>
#include <cstdint>
constexpr int SR = 44100;
// SND5: band-limited resampling by `ratio` (output / input rate): a Blackman-windowed sinc, 16 zero crossings of the
// lower rate, tabulated (512 steps per input sample, linearly interpolated). miniaudio's linear resampler, used before,
// took -1.8 dB at 6 kHz and -5 dB at 10 kHz off the 24 kHz recordings
std::vector<float> resampleSinc(const std::vector<float>& x, double ratio) {
  if (x.empty() || std::fabs(ratio - 1.0) < 1e-9) return x;
  const double fc = 0.5 * std::min(1.0, ratio) * 0.96;   // cutoff, cycles per input sample
  const int H = (int)std::ceil(8.0 / fc), T = 512;       // half-width (input samples), table steps per input sample
  std::vector<float> tab((size_t)H * T + 2);
  for (size_t i = 0; i < tab.size(); ++i) { double u = (double)i / T, w = std::min(1.0, u / H);
    double s = u < 1e-12 ? 2 * fc : std::sin(2 * 3.14159265358979 * fc * u) / (3.14159265358979 * u);
    tab[i] = (float)(s * (0.42 + 0.5 * std::cos(3.14159265358979 * w) + 0.08 * std::cos(2 * 3.14159265358979 * w))); }
  std::vector<float> y((size_t)(x.size() * ratio));
  for (size_t n = 0; n < y.size(); ++n) {
    double t = n / ratio; long c = (long)std::floor(t); double s = 0;
    for (long k = std::max(0L, c - H + 1); k <= std::min((long)x.size() - 1, c + H); ++k) {
      double u = std::fabs(t - k) * T; size_t i = (size_t)u; if (i + 1 >= tab.size()) continue; double f = u - i;
      s += x[k] * (tab[i] * (1 - f) + tab[i + 1] * f);
    }
    y[n] = (float)s;
  }
  return y;
}
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

struct LimiterNode { ma_node_base base; float g = 1, rel = 0; ma_uint32 ch = 2; };   // SND5 (Audio::Impl::lim)
// SND5: the gunshot reverb (Audio::Impl::verb): the dry sound through unchanged, plus a slap-back (one early reflection)
// and a damped Schroeder tail (4 parallel combs into 2 series all-passes per side, left / right 23 samples apart).
// verb[0]: every gun, a small send; verb[1]: machine guns (M249, MG 42, M2HB); verb[2]: the .50 sniper, more and longer.
// Sends and tails: content/audio/custom/mix.json "reverb". Audio::setRoom(1) (tunnels, Backrooms): a shorter slap-back,
// a longer, denser tail. The delay lines are allocated once at their longest (outdoors) and only their used length
// changes, so a retune never reallocates under the audio thread
struct VerbNode {
  ma_node_base base; ma_uint32 ch = 2; float sr = 44100, send = 0.1f, rtOut = 1.2f, room = -1;
  std::vector<float> slap; size_t sp = 0, slapLen = 1; float slapG = 0.3f;
  struct Line { std::vector<float> b; size_t n = 1, i = 0; float fb = 0.8f, lp = 0; };
  Line comb[2][4], ap[2][2];
  void setRoom(float r, bool force = false) {
    if (!force && std::fabs(r - room) < 1e-3f) return; room = r;
    const int cl[4] = {1557, 1617, 1491, 1422}, al[2] = {556, 441}; const float sc0 = sr / 44100.0f, sc = sc0 * (1.0f - 0.25f * r), rt = rtOut * (1.0f + 0.4f * r);
    for (int s = 0; s < 2; ++s) {
      for (int k = 0; k < 4; ++k) { Line& c = comb[s][k]; if (c.b.empty()) c.b.assign((size_t)((cl[k] + 23 * s) * sc0) + 1, 0.0f); c.n = std::min(c.b.size(), (size_t)((cl[k] + 23 * s) * sc)); c.fb = std::pow(10.0f, -3.0f * c.n / (rt * sr)); }
      for (int k = 0; k < 2; ++k) { Line& c = ap[s][k]; if (c.b.empty()) c.b.assign((size_t)((al[k] + 23 * s) * sc0) + 1, 0.0f); c.n = std::min(c.b.size(), (size_t)((al[k] + 23 * s) * sc)); }
    }
    if (slap.empty()) slap.assign((size_t)(0.2f * sr), 0.0f);
    slapLen = (size_t)((0.11f - 0.075f * r) * sr); slapG = 0.3f + 0.1f * r;
  }
};
struct Audio::Impl {
  ma_engine engine;
  std::map<std::string, Buf> bufs;
  struct Event { float minDist = 1;   /* full volume inside this range (m): big sounds carry */ std::vector<Buf> v; float gain = 1; std::string kind; int last = -1;
                 std::vector<Buf> st;      // SND5 st: interleaved stereo of each variant (gun shots), played as is when at the listener
                 int bus = -1; float rolloff = -1, earM = 2; bool duck = false;
                 float minGap = 0; int maxVoices = 0; };   // SND9: a very fast gun: at most one shot per minGap s per spot, bodies overlapping up to maxVoices   // SND9: its own reverb bus (1-4), roll-off, at-the-ear range, ducks the beds
  std::map<std::string, Event> events;
  std::mt19937 pick{20260606};
  vec3 listener{0};
  struct Voice { ma_audio_buffer buf; ma_sound snd; bool used = false; std::string event; double start = 0; int bus = 0; };
  std::map<int, double> casingAt;   // SND4: per shooter, when its last case sound was scheduled
  std::map<std::string, double> rapidAt;   // SND9: rapid-fire event + 4 m cell -> when it last sounded
  float casingGain = 2.0f;           // SND6: x on the case sound (content/audio/custom/mix.json casing_gain)
  ma_context ctx; bool ctxOn = false;   // TST1: the null backend for test runs
  ma_audio_buffer hbAB; ma_sound hb; bool hbOn = false, hbPlaying = false; Buf hbBuf; double hbT = -1;   // SND13 heartbeat loop
  ma_sound music; bool musicOn = false, musicPlaying = false;   // SND21 menu track (streamed)
  float reloadGain = 2.24f; std::map<std::string, float> reloadTrim;   // WP6: x on reload / magazine / clip / bolt / pump foley (mix.json "reload": gain_db, trim_db per weapon id)
  Voice voices[48];
  Buf windBuf, leafBuf; float windVol = 0.0f, leafVol = 0.0f;   // the wind bed gave way to the battlefield recording (setBed)
  ma_audio_buffer windAB, leafAB; ma_sound wind, leaves;
  ma_sound bed[2]; ma_audio_buffer bedAB[2]; Buf bedBuf[2]; bool bedOn[2] = {false, false}; std::string bedFile[2]; float bedVol[2] = {0, 0};
  Voice loops[16];
  struct Amb { Buf b; ma_audio_buffer ab; ma_sound snd; bool on = false; };   // BTA: map ambience voices (ambVoice), never moved
  std::map<std::string, std::unique_ptr<Amb>> amb;
  ma_lpf_node lpf; bool lpfOk = false; float lpfCut = -1;   // under-water muffle
  // SND5: sounds reach the limiter directly, through the muffle only while under water (its 20 kHz "open" biquad still
  // took the top octave); the limiter (instant attack, 40 ms release, ceiling 0.98) keeps loud overlaps from clipping
  LimiterNode lim; bool limOk = false, muffled = false;
  VerbNode verb[4]; bool verbOk = false;
  double duckT = -1e9;   // SND5: the last gunshot within 60 m (the ambience beds duck under it)
  ma_node* pre() { return muffled && lpfOk ? (ma_node*)&lpf : out(); }
  ma_node* out() { return limOk ? (ma_node*)&lim : ma_engine_get_endpoint(&engine); }
  void route(ma_sound& s, int bus = 0) { ma_node_attach_output_bus(&s, 0, bus > 0 && verbOk ? (ma_node*)&verb[bus - 1] : pre(), 0); }
  void rerouteAll() {
    if (verbOk) for (auto& v : verb) ma_node_attach_output_bus(&v, 0, pre(), 0);
    for (auto& v : voices) if (v.used) route(v.snd, v.bus);
    for (auto& v : loops) if (v.used) route(v.snd);
    for (int k = 0; k < 2; ++k) if (bedOn[k]) route(bed[k]);
    for (auto& [id, a] : amb) if (a->on) route(a->snd);
    route(wind); route(leaves);
  }
};
static void limiterProcess(ma_node* node, const float** in, ma_uint32* inFrames, float** outp, ma_uint32* outFrames) {
  auto* L = (LimiterNode*)node; ma_uint32 n = std::min(*inFrames, *outFrames), C = L->ch; const float* x = in[0]; float* y = outp[0];
  for (ma_uint32 i = 0; i < n; ++i) {
    float a = 0; for (ma_uint32 c = 0; c < C; ++c) a = std::max(a, std::fabs(x[i * C + c]));
    float need = a > 0.98f ? 0.98f / a : 1.0f; L->g = need < L->g ? need : L->g + (1.0f - L->g) * L->rel;
    for (ma_uint32 c = 0; c < C; ++c) y[i * C + c] = x[i * C + c] * L->g;
  }
  *inFrames = n; *outFrames = n;
}
static ma_node_vtable gLimiterVt = {limiterProcess, nullptr, 1, 1, 0};
static void verbProcess(ma_node* node, const float** in, ma_uint32* inFrames, float** outp, ma_uint32* outFrames) {
  auto* V = (VerbNode*)node; ma_uint32 n = std::min(*inFrames, *outFrames), C = V->ch; const float* x = in[0]; float* y = outp[0];
  for (ma_uint32 i = 0; i < n; ++i) {
    float m = 0; for (ma_uint32 c = 0; c < C; ++c) m += x[i * C + c]; m *= V->send / C;
    size_t rd = (V->sp + V->slap.size() - V->slapLen) % V->slap.size(); float early = V->slap[rd] * V->slapG; V->slap[V->sp] = m; V->sp = (V->sp + 1) % V->slap.size();
    float wet[2];
    for (int s = 0; s < 2; ++s) {
      float acc = 0;
      for (auto& c : V->comb[s]) { if (c.i >= c.n) c.i = 0; float o = c.b[c.i]; c.lp = o * 0.6f + c.lp * 0.4f; c.b[c.i] = m * 0.25f + c.lp * c.fb; ++c.i; acc += o; }
      for (auto& a : V->ap[s]) { if (a.i >= a.n) a.i = 0; float o = a.b[a.i]; a.b[a.i] = acc + o * 0.5f; ++a.i; acc = o - acc; }
      wet[s] = acc + early;
    }
    for (ma_uint32 c = 0; c < C; ++c) y[i * C + c] = x[i * C + c] + wet[c & 1];
  }
  *inFrames = n; *outFrames = n;
}
static ma_node_vtable gVerbVt = {verbProcess, nullptr, 1, 1, 0};

bool Audio::init() {
  p = new Impl();
  ma_engine_config cfg = ma_engine_config_init();
  cfg.sampleRate = SR;
  if (std::getenv("NMK_AUDIO_FIDELITY")) { cfg.noDevice = MA_TRUE; cfg.channels = 2; }   // SND5 diagnostics: the mix is read back
  else if (silent) {   // TST1: a test run: miniaudio's null device (a thread consumes the mix in real time); no sound card output
    ma_backend be[1] = {ma_backend_null}; ma_context_config cc = ma_context_config_init();
    if (ma_context_init(be, 1, &cc, &p->ctx) == MA_SUCCESS) { p->ctxOn = true; cfg.pContext = &p->ctx; logf("audio: null device (test run)"); }
    else { cfg.noDevice = MA_TRUE; cfg.channels = 2; logf("audio: null device unavailable: no device (test run)"); }
  }
  if (ma_engine_init(&cfg, &p->engine) != MA_SUCCESS) { logf("audio: no device, sound disabled"); if (p->ctxOn) ma_context_uninit(&p->ctx); delete p; p = nullptr; return false; }
  if (!silent && !cfg.noDevice) if (ma_device* d = ma_engine_get_device(&p->engine)) logf("audio: output device \"%s\"", d->playback.name);   // TST1
  {   // muffle stage (swim.cpp): open at 20 kHz until the camera goes under water
    ma_lpf_node_config lc = ma_lpf_node_config_init(ma_engine_get_channels(&p->engine), ma_engine_get_sample_rate(&p->engine), 20000.0, 2);
    {   // SND5: the limiter in front of the endpoint
      ma_uint32 ch = ma_engine_get_channels(&p->engine); p->lim.ch = ch; p->lim.rel = 1.0f - std::exp(-1.0f / (0.04f * ma_engine_get_sample_rate(&p->engine)));
      ma_node_config nc = ma_node_config_init(); nc.vtable = &gLimiterVt; nc.pInputChannels = &ch; nc.pOutputChannels = &ch;
      p->limOk = ma_node_init(ma_engine_get_node_graph(&p->engine), &nc, nullptr, &p->lim) == MA_SUCCESS &&
                 ma_node_attach_output_bus(&p->lim, 0, ma_engine_get_endpoint(&p->engine), 0) == MA_SUCCESS;
    }
    p->lpfOk = ma_lpf_node_init(ma_engine_get_node_graph(&p->engine), &lc, nullptr, &p->lpf) == MA_SUCCESS &&
               ma_node_attach_output_bus(&p->lpf, 0, p->out(), 0) == MA_SUCCESS;
    {   // SND5: the gun reverbs (defaults; content/audio/custom/mix.json "reverb" retunes them at load): every gun (send 0.12,
        // 1.2 s), machine guns (SND6: halved, 0.35 -> 0.175, 1.5 -> 1.3 s), the .50 sniper (0.25, 1.8 s)
      ma_uint32 ch = ma_engine_get_channels(&p->engine); p->verbOk = true;
      const float sends[4] = {0.12f, 0.175f, 0.25f, 0.3f}, tails[4] = {1.2f, 1.3f, 1.8f, 2.0f};   // SND9 [3]: tank guns and shell hits
      for (int k = 0; k < 4; ++k) {
        VerbNode& v = p->verb[k]; v.ch = ch; v.sr = (float)ma_engine_get_sample_rate(&p->engine); v.send = sends[k]; v.rtOut = tails[k]; v.setRoom(0);
        ma_node_config nc = ma_node_config_init(); nc.vtable = &gVerbVt; nc.pInputChannels = &ch; nc.pOutputChannels = &ch;
        p->verbOk = p->verbOk && ma_node_init(ma_engine_get_node_graph(&p->engine), &nc, nullptr, &v) == MA_SUCCESS && ma_node_attach_output_bus(&v, 0, p->out(), 0) == MA_SUCCESS;
      }
    }
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
  { Buf b = mk(0.22f); addSteps(b, 120, 0.2f, 0.8f, r); addClang(b, 0.004f, 1850, 0.35f, 0.05f, r); addClang(b, 0.006f, 2900, 0.15f, 0.03f, r); normalize(b, 0.32f); p->bufs["step_metal"] = b; }   // SND10: the robot commando's step
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
  for (auto& [id, a] : p->amb) if (a->on) { ma_sound_uninit(&a->snd); ma_audio_buffer_uninit(&a->ab); }
  p->amb.clear();
  if (p->hbOn) { ma_sound_uninit(&p->hb); ma_audio_buffer_uninit(&p->hbAB); p->hbOn = false; }
  if (p->musicOn) { ma_sound_uninit(&p->music); p->musicOn = false; }
  ma_sound_uninit(&p->wind); ma_sound_uninit(&p->leaves);
  if (p->lpfOk) ma_lpf_node_uninit(&p->lpf, nullptr);
  ma_audio_buffer_uninit(&p->windAB); ma_audio_buffer_uninit(&p->leafAB);
  ma_engine_uninit(&p->engine);
  if (p->ctxOn) ma_context_uninit(&p->ctx);
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
  for (auto& [name, e] : jkey(m, "events").items()) {
    Impl::Event ev; ev.gain = e.value("gain", 1.0f); ev.kind = e.value("kind", "");
    for (auto& v : e["variants"]) {
      std::string bank = v.value("bank", ""); for (const char* pre : {"ww2_weapon_", "ww2_", "cmn_"}) if (bank.rfind(pre, 0) == 0) { bank = bank.substr(std::strlen(pre)); break; }
      fs::path wav = dir / "samples" / (bank + "_" + std::to_string(v.value("index", 0)) + ".wav");
      Buf b; if (loadWavMono(wav, b) && !b.empty()) { ev.v.push_back(std::move(b)); ++samplesLoaded; } else ++samplesMissing;
    }
    if (ev.kind == "shot") for (auto& b : ev.v) { float pk = 1e-4f; for (float x : b) pk = std::max(pk, std::fabs(x)); if (pk < 0.95f) for (float& x : b) x *= 0.95f / pk; }   // SND5: bank gunshots up to 0.95 peak
    if (!ev.v.empty()) { p->events[name] = std::move(ev); ++eventsLoaded; }
  }
  logf("audio: %d events from %d samples (%d missing: run tools/fsb5.py extract)", eventsLoaded, samplesLoaded, samplesMissing);
  return eventsLoaded;
}
// User-supplied recordings replace events: footsteps.mp3 (a walk: cut into single steps at its onsets) for every
// dry step, door_open.mp3 for doors / windows / shutters opening at half volume, zombie.mp3 as "zombie_groan".
int Audio::loadCustom(const fs::path& dir) {
  if (!p) return 0;
  // SND5: decoded at the file's own rate and channels, resampled to 44.1 kHz by resampleSinc; out = (L + R) / 2 (the
  // spatialised voice), L / R (optional) the channels themselves (a mono file: both the same)
  // CACHE1: every recording of the folder decoded and resampled in parallel first (one at a time took ~2 s at startup);
  // decodeSt takes the prepared result (a file not prepared decodes here as before)
  struct Pre { bool ok = false; Buf out, L, R; };
  std::map<std::string, Pre> pre;
  auto decodeRaw = [&](const fs::path& f, Pre& r) {
    ma_decoder_config dc = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_uint64 frames = 0; void* data = nullptr;
    if (ma_decode_file(f.string().c_str(), &dc, &frames, &data) != MA_SUCCESS || !data) return;
    const float* d = (const float*)data; const ma_uint32 C = std::max<ma_uint32>(1, dc.channels);
    Buf ch[2]; for (int c = 0; c < 2; ++c) { ch[c].resize(frames); for (ma_uint64 i = 0; i < frames; ++i) ch[c][i] = d[i * C + std::min<ma_uint32>(c, C - 1)]; }
    ma_free(data, nullptr);
    if (dc.sampleRate != (ma_uint32)SR && dc.sampleRate > 0) for (auto& x : ch) x = resampleSinc(x, (double)SR / dc.sampleRate);
    r.out.resize(ch[0].size()); for (size_t i = 0; i < r.out.size(); ++i) r.out[i] = 0.5f * (ch[0][i] + ch[1][i]);
    r.L = std::move(ch[0]); r.R = std::move(ch[1]); r.ok = true;
  };
  {
    std::error_code ec; std::vector<fs::path> fl;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (it->is_directory(ec) && it->path().filename() == "music") { it.disable_recursion_pending(); continue; }   // SND21: streamed, never pre-decoded
      std::string x = it->path().extension().string(); if (x == ".mp3" || x == ".wav" || x == ".ogg" || x == ".flac") fl.push_back(it->path());
    }
    for (auto& f : fl) pre[fs::relative(f, dir, ec).generic_string()];
    std::atomic<size_t> next{0}; std::vector<std::thread> th;
    unsigned nt = std::clamp(std::thread::hardware_concurrency(), 1u, 12u);
    for (unsigned t = 0; t < nt && t < fl.size(); ++t) th.emplace_back([&] { for (size_t i; (i = next++) < fl.size();) { std::error_code e2; decodeRaw(fl[i], pre.at(fs::relative(fl[i], dir, e2).generic_string())); } });
    for (auto& t : th) t.join();
  }
  auto decodeSt = [&](const char* file, Buf& out, Buf* L, Buf* R) {
    auto pit = pre.find(fs::path(file).generic_string());
    if (pit != pre.end() && pit->second.ok) { out = pit->second.out; if (L) *L = pit->second.L; if (R) *R = pit->second.R; return true; }
    ma_decoder_config dc = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_uint64 frames = 0; void* data = nullptr;
    if (ma_decode_file((dir / file).string().c_str(), &dc, &frames, &data) != MA_SUCCESS || !data) return false;
    const float* d = (const float*)data; const ma_uint32 C = std::max<ma_uint32>(1, dc.channels);
    Buf ch[2]; for (int c = 0; c < 2; ++c) { ch[c].resize(frames); for (ma_uint64 i = 0; i < frames; ++i) ch[c][i] = d[i * C + std::min<ma_uint32>(c, C - 1)]; }
    ma_free(data, nullptr);
    if (dc.sampleRate != (ma_uint32)SR && dc.sampleRate > 0) for (auto& x : ch) x = resampleSinc(x, (double)SR / dc.sampleRate);
    out.resize(ch[0].size()); for (size_t i = 0; i < out.size(); ++i) out[i] = 0.5f * (ch[0][i] + ch[1][i]);
    if (L) *L = std::move(ch[0]);
    if (R) *R = std::move(ch[1]);
    return !out.empty();
  };
  auto decode = [&](const char* file, Buf& out) { return decodeSt(file, out, nullptr, nullptr); };
  // SND6 / SND7: content/audio/custom/mix.json (levels, reverb, per-gun trims; see its note)
  json mx; try { std::ifstream f(dir / "mix.json"); if (f) mx = json::parse(f); } catch (...) { logf("audio: custom/mix.json unreadable, defaults kept"); }
  if (!mx.is_object()) mx = json::object();
  const json mixGuns = mx.contains("guns") && mx["guns"].is_object() ? mx["guns"] : json::object();   // id: gain, or {gain, body}
  if (mx.contains("reload") && mx["reload"].is_object()) {   // WP6: reload foley level (dB) and per-weapon trims
    const json& rl = mx["reload"]; p->reloadGain = std::pow(10.0f, rl.value("gain_db", 7.0f) / 20.0f);
    for (auto& [k, v] : rl.value("trim_db", json::object()).items()) if (v.is_number()) p->reloadTrim[k] = std::pow(10.0f, v.get<float>() / 20.0f);
  }
  int used = 0; Buf b;
  struct StepSet { const char* file; std::vector<const char*> names; size_t gap = 25; };   // gap: onsets at least this many 10 ms windows apart
  // BTA: winter surfaces (bta/steps_*.mp3: single steps 0.95 s apart, tools/bta_audio_prep.py; 0.6 s keeps a crunch's second transient in its step)
  for (auto& ss : {StepSet{"footsteps.mp3", {"step", "step_stone", "step_wood"}}, StepSet{"grass_steps.mp3", {"step_grass"}}, StepSet{"spider_steps.mp3", {"step_spider"}},
                   StepSet{"bta/steps_snow.mp3", {"step_snow"}, 60}, StepSet{"bta/steps_snow_deep.mp3", {"step_snow_deep"}, 60}, StepSet{"bta/steps_slush.mp3", {"step_slush"}, 60},
                   StepSet{"bta/steps_mud.mp3", {"step_mud"}, 60}, StepSet{"bta/steps_shallow.mp3", {"step_shallow"}, 60}}) if (decode(ss.file, b)) {
    const int W = SR / 100; std::vector<float> env(b.size() / W, 0.0f); float peak = 0;   // 10 ms loudness windows
    for (size_t k = 0; k < env.size(); ++k) { float e = 0; for (int j = 0; j < W; ++j) e = std::max(e, std::fabs(b[k * W + j])); env[k] = e; peak = std::max(peak, e); }
    std::vector<size_t> on;   // onsets: loud after a quiet window, at least 0.25 s apart
    for (size_t k = 1; k < env.size(); ++k) if (env[k] > 0.3f * peak && env[k - 1] <= 0.3f * peak && (on.empty() || k - on.back() >= ss.gap)) on.push_back(k);
    Impl::Event ev; ev.kind = "step";
    if (mx.contains("steps") && mx["steps"].is_object()) ev.gain = mx["steps"].value(ss.names[0], 1.0f);   // BTA: per-surface level
    for (size_t i = 0; i < on.size() && ev.v.size() < 24; ++i) {
      size_t a = (on[i] > 1 ? on[i] - 1 : 0) * W, e = std::min({b.size(), a + (size_t)(0.45f * SR), i + 1 < on.size() ? (on[i + 1] - 1) * W : b.size()});
      if (e <= a + (size_t)W) continue;
      Buf s(b.begin() + a, b.begin() + e); float pk = 1e-4f; for (float x : s) pk = std::max(pk, std::fabs(x));
      size_t f = std::min(s.size(), (size_t)(0.03f * SR));
      for (size_t j = 0; j < s.size(); ++j) s[j] *= 0.5f / pk * (j + f >= s.size() ? (float)(s.size() - j) / f : 1.0f);   // even level, 30 ms fade-out
      ev.v.push_back(std::move(s));
    }
    if (!ev.v.empty()) { ++used; for (const char* n : ss.names) { p->events[n] = ev; } }
  }
  {   // BTA: wood creaks near old sheds (map_ambience.cpp): one variant per file
    Impl::Event ev; ev.kind = "amb";
    for (const char* f : {"bta/creak_1.mp3", "bta/creak_2.mp3", "bta/creak_3.mp3", "bta/creak_4.mp3"}) if (decode(f, b)) ev.v.push_back(b);
    if (!ev.v.empty()) { ++used; p->events["amb_creak"] = std::move(ev); }
  }
  for (auto [name, files] : {std::pair<const char*, std::vector<const char*>>{"wx_thunder_near", {"wx/thunder_near1.mp3", "wx/thunder_near2.mp3"}},
                              {"wx_thunder_far", {"wx/thunder_far1.mp3"}}}) {   // W1 weather: thunder (weather_audio.cpp), on the ambience bus
    Impl::Event ev; ev.kind = "amb";
    for (const char* f : files) if (decode(f, b)) ev.v.push_back(b);
    if (!ev.v.empty()) { ++used; p->events[name] = std::move(ev); }
  }
  if (decode("door_open.mp3", b)) { ++used; Impl::Event ev; ev.v.push_back(b); ev.gain = 0.5f; for (const char* n : {"wood_open", "glass_open", "shutter_open", "steel_open"}) p->events[n] = ev; }
  // gun shots (guns/*.mp3): one recording per weapon group. A burst recording gives its last shot (the one with the tail);
  // automatic fire re-triggers it per round like every other shot event.
  // SND4: every rifle its own shot. take: which shot of a recording holding several (Rifle3: four), -1 the first;
  // pitch / tilt / tail: a variant derived from a recording another rifle uses (resampled x pitch; tilt < 0 darker, > 0
  // brighter; tail: seconds kept)
  struct GunSet { const char* file; bool burst; std::vector<const char*> ids; int take = -1; float pitch = 1, tilt = 0, tail = 2.5f; float body = 0; float gain = -1; int vars = 0; };   // SND16 vars > 0: file is a printf pattern of vars variants (mg_shot_%d.wav), one event picking a different one each round   // SND5 body: low-mid weight and density (M249, MG 42); SND14 gain >= 0: the set's own level and body (mix.json's per-gun trims are for the older recordings)
  const GunSet guns[] = {
    {"guns/pistol_a.mp3", false, {"luger"}},
    {"guns/pistol_c.mp3", false, {"modern_pistol2"}},
    {"guns/pistol_b.mp3", false, {"m1911a1"}},
    {"guns/pistol_9mm.mp3", false, {"modern_pistol", "modern_ar15", "modern_pistol3"}},   // the AR-15 keeps the Springfield XD single shot (user request)
    {"guns/semi_rifle.mp3", false, {"modern_sniper_semi"}},
    {"guns/semi_rifle.mp3", false, {"modern_scar"}, -1, 0.9f, -0.5f, 2.2f},   // derived: SCAR-H, lower and darker
    {"guns/auto_rifle_burst.mp3", true, {"modern_mp7"}},
    {"guns/carbine.mp3", false, {"m1_carbine"}},
    // SND5: the Garand recording retired (user request): its guns play Rifle.mp3, a few per cent apart in pitch, no tone change
    {"guns/rifle_a.mp3", false, {"m1_garand"}, -1, 1.03f},
    {"guns/rifle_a.mp3", false, {"m1c_garand"}, -1, 0.97f},
    {"guns/rifle_a.mp3", false, {"m1918a2_bar"}, -1, 0.96f},
    {"guns/rifle_a.mp3", false, {"fg42"}, -1, 1.04f},
    {"guns/sniper_heavy.mp3", false, {"modern_sniper_50"}, -1, 1, 0, 2.5f, 4.0f},   // the heavy sniper shot (user request); SND6: the loudest small arm: most body
    {"guns/sniper_m24.mp3", false, {"modern_sniper_bolt"}},   // Sniper.mp3: the M24's own
    {"guns/sniper_m24.mp3", false, {"modern_sniper_3"}, -1, 1.12f, 0.3f, 1.8f},   // derived: scout rifle, higher, shorter (SND8: 1.08 -> 1.12, clear of the Kar98k)
    {"guns/sniper_m24.mp3", false, {"kar98k"}, -1, 1.06f},   // SND8: the M24 recording 6 % higher (user request), full level, no tone change
    {"guns/sniper_m24.mp3", false, {"kar98k_zf39"}, -1, 0.96f},   // SND8: the M24 recording 4 % lower
    // SND15 (user request): the AK recording (Rifle2.mp3) is gone; every gun that fired with it takes the CZ Scorpion's recording
    // (SMG.mp3), levelled as the rifle it is (own gain / body, measured offline to the AK shot's level), a few per cent apart
    {"guns/smg_a.mp3", false, {"modern_ak"}, -1, 1.00f, 0, 2.5f, 2.5f, 1.21f},
    {"guns/smg_a.mp3", false, {"modern_ak12"}, -1, 0.98f, 0, 2.5f, 2.5f, 1.23f},
    {"guns/smg_a.mp3", false, {"stg44"}, -1, 1.03f, 0, 2.5f, 2.5f, 1.22f},
    {"guns/smg_a.mp3", false, {"modern_lmg"}, -1, 0.96f, 0, 2.5f, 3.0f, 1.24f},   // the M249, heavier (SND16: its fallback when snd16/ is missing)
    {"guns/rifle_c.mp3", false, {"modern_m16"}, 0, 1, 0, 2.5f, 3.0f},   // Rifle3.mp3 holds four shots: one each; SND7: thin and quiet: body (mix.json)
    {"guns/rifle_c.mp3", false, {"modern_m4_irons"}, 2, 1, 0, 2.5f, 3.0f},
    {"guns/rifle_c.mp3", false, {"modern_fal"}, 3, 1, 0, 2.5f, 3.0f},
    {"guns/shotgun.mp3", false, {"modern_shotgun", "modern_shotgun2"}},
    {"guns/hmg_burst.mp3", true, {"m2hb_carry"}, -1, 1, 0, 2.5f, 3.0f},   // SND6: body (it measured 12 dB under the rifles)
    {"guns/rifle_a.mp3", false, {"mg42"}, -1, 0.84f, 0, 2.5f, 2.2f},   // SND8: Rifle.mp3 pitched down (user request), one clean shot per round, machine-gun body
    {"guns/smg_burst.mp3", true, {"modern_uzi"}},
    // SND4: every SMG its own shot (SMG.mp3, SMG1.mp3: single shots)
    {"guns/smg_a.mp3", false, {"mp40"}, -1, 1, 0, 1.2f},   // SMG.mp3
    {"guns/smg_a.mp3", false, {"modern_scorpion"}, -1, 1.12f, 0.4f, 1.0f},   // derived: the Scorpion, higher and brighter
    {"guns/smg_b.mp3", false, {"m1_thompson"}},   // SMG1.mp3
    {"guns/smg_b.mp3", false, {"m3_grease_gun"}, -1, 0.88f, -0.5f, 2.5f},   // derived: the grease gun, lower and darker
    {"guns/smg_b.mp3", false, {"sten"}, -1, 1.04f},   // NG3: the Sten's fallback (snd14 missing)
    // SND9: gun-shot-1 (user request) for the vehicle rapid-fire guns: the Abrams roof minigun (shot_minigun: higher, short and
    // tight) and the Apache's 30 mm chain gun (shot_cannon30: lower, heavier); "rapid" below
    {"guns/gun_shot_1.mp3", false, {"minigun", "modern_minigun"}, -1, 1.15f, 0, 0.3f, 1.5f},   // MG13: the hand-held minigun borrows the vehicle minigun's shot (stand-in; the performance session owns sound)
    {"guns/gun_shot_1.mp3", false, {"cannon30"}, -1, 0.85f, 0, 0.6f, 3.0f},
    // MG12 (borrowed, derived): the sawn-off on the 870's shot a little lower; the SMG-9 on the MP 40's, lower and darker
    // WP7 (user request): the .50 AE and the .357 take the shot the FN 502 Tactical had (the suppressed .22's recording, 12 % higher,
    // brighter, short tail); the revolver 4 % under the Desert Eagle so the two stay apart
    {"guns/pistol_c.mp3", false, {"modern_deagle"}, -1, 1.12f, 0.2f, 1.6f},
    {"guns/pistol_c.mp3", false, {"modern_revolver"}, -1, 1.075f, 0.2f, 1.6f},
    {"guns/shotgun.mp3", false, {"modern_sawnoff"}, -1, 0.93f, -0.2f},
    {"guns/smg_a.mp3", false, {"modern_smg9"}, -1, 0.94f, -0.2f, 1.1f},
    // MG12 batch 2 (borrowed, derived): the compensated Glock on the 9 mm shot, higher and brighter; the Makarov on the Luger's,
    // higher; the Mk 23 on the M1911's, a little lower
    {"guns/pistol_9mm.mp3", false, {"modern_glock34"}, -1, 1.06f, 0.3f},
    {"guns/pistol_b.mp3", false, {"modern_mark23"}, -1, 0.95f, -0.1f},
    // MG12 batch 3 (borrowed, derived; both suppressed): the Saiga on the 870's shot, higher, much darker and shorter; the VSK on the
    // suppressed .22's, lower and darker (heavier)
    {"guns/pistol_c.mp3", false, {"modern_vsk"}, -1, 0.78f, -0.4f, 1.2f, 1.0f},
    // SW1: synthesised blaster shots (tools: sweep + FM + odd harmonics + spring tail; no recordings): carbine, long rifle, robot blaster
    // NG1: the returning Makarov (its MG12 set), the .22 tactical pistol's own shot (the 9 mm recording raised to a rimfire crack:
    // the Desert Eagle / .357 keep the FN 502's old one), the 9 mm service pistol on the 9 mm shot a little lower and darker,
    // the .45 SMG on the Thompson's (SMG1.mp3) lower; snd14 sets below take over where present
    {"guns/pistol_a.mp3", false, {"modern_makarov"}, -1, 1.08f, 0.1f},
    {"guns/pistol_9mm.mp3", false, {"modern_fn502"}, -1, 1.22f, 0.4f, 0.9f},
    {"guns/pistol_9mm.mp3", false, {"modern_pistol4"}, -1, 0.95f, -0.15f},
    {"guns/smg_b.mp3", false, {"modern_smg45"}, -1, 0.92f, -0.2f, 2.0f},
    {"guns/blaster_a.wav", false, {"blaster_carbine"}, -1, 1, 0, 0.6f},
    {"guns/blaster_b.wav", false, {"blaster_rifle"}, -1, 1, 0, 0.8f},
    {"guns/blaster_c.wav", false, {"blaster_robot"}, -1, 1, 0, 0.7f},
    // SND14 (user request: the user's new recordings, local-only content/audio/custom/snd14/, licence unknown): later sets replace
    // the ones above for these guns when the file is there; without it the old shot stays. Each set's gain / body bring the
    // first 200 ms to the level the gun had (measured offline through this pipeline): the new recordings are peakier
    {"snd14/ar15_shot.wav", false, {"modern_ar15"}, -1, 1.00f, 0, 1.0f, 0.0f, 1.17f},   // AR15Shot: the AR pattern
    {"snd14/ar15_shot.wav", false, {"modern_m4_irons"}, -1, 1.04f, 0, 1.0f, 1.5f, 1.18f},
    {"snd14/ar15_shot.wav", false, {"modern_m16"}, -1, 0.97f, 0, 1.0f, 1.5f, 1.18f},
    {"snd14/rifle_shot_1.wav", false, {"modern_fal"}, -1, 1.00f, 0, 1.6f, 2.5f, 1.14f},   // rifleshot (the darkest): battle rifles
    {"snd14/rifle_shot_1.wav", false, {"modern_scar"}, -1, 1.05f, 0, 1.6f, 2.5f, 1.21f},
    {"snd14/rifle_shot_2.wav", false, {"m1_garand"}, -1, 1.00f, 0, 1.6f, 3.0f, 1.19f},   // rifleshot2: the Garand family and the BAR
    {"snd14/rifle_shot_2.wav", false, {"m1c_garand"}, -1, 0.97f, 0, 1.6f, 3.0f, 1.19f},
    {"snd14/rifle_shot_2.wav", false, {"m1918a2_bar"}, -1, 0.94f, 0, 1.6f, 3.0f, 1.18f},
    {"snd14/rifle_shot_3.wav", false, {"modern_sniper_semi"}, -1, 1.00f, 0, 1.6f, 3.0f, 1.23f},   // rifleshot3 (the brightest): marksman / FG 42
    {"snd14/rifle_shot_3.wav", false, {"fg42"}, -1, 1.04f, 0, 1.6f, 3.5f, 1.23f},
    {"snd14/smg_shot_1.wav", false, {"mp40"}, -1, 1.00f, 0, 1.0f, 5.5f, 1.23f},   // SMGShot
    {"snd14/smg_shot_1.wav", false, {"m3_grease_gun"}, -1, 0.90f, 0, 1.0f, 5.5f, 1.23f},
    {"snd14/smg_shot_1.wav", false, {"sten"}, -1, 1.06f, 0, 1.0f, 5.5f, 1.23f},   // NG3: the Sten (9 mm): the MP40's take a little higher
    {"snd14/smg_shot_2.wav", false, {"modern_uzi"}, -1, 1.00f, 0, 1.0f, 5.0f, 1.24f},   // SMGShot1 (bright, as its old burst)
    {"snd14/smg_shot_3.wav", false, {"m1_thompson"}, -1, 0.94f, 0, 1.0f, 2.0f, 1.10f},   // SMGShot2
    {"snd14/smg_shot_3.wav", false, {"modern_smg9"}, -1, 1.03f, 0, 1.0f, 2.0f, 1.14f},
    {"snd14/smg_shot_3.wav", false, {"modern_smg45"}, -1, 0.88f, 0, 1.0f, 2.0f, 1.12f},   // NG1: SMGShot2 (the Thompson / SMG-9 file) lower: the .45 SMG
    // SND15 (user request; local-only content/audio/custom/snd15/, licence being asked): four pistol takes spread so neighbours differ
    // (A pistol.mp3, B pistol (2).mp3 = SND14's SMGShot1, C pistol(3).mp3 the heavy one, D Pistol(4).mp3 the thin crack); the
    // Desert Eagle / .357 keep the FN 502's old shot (WP7); the shotguns on Shotgun.mp3; suppressedRifle on the integrally
    // suppressed MP7 / VSK and, as "shot_suppressed_rifle", on any rifle / carbine / SMG / LMG / sniper with a suppressor fitted
    {"snd15/pistol_1.wav", false, {"modern_pistol"}, -1, 1.00f, 0, 1.2f, 1.5f, 1.15f},   // A
    {"snd15/pistol_1.wav", false, {"modern_glock34"}, -1, 1.05f, 0, 1.2f, 1.5f, 1.13f},
    {"snd15/pistol_1.wav", false, {"modern_makarov"}, -1, 0.92f, 0, 1.2f, 1.0f, 1.24f},
    {"snd14/smg_shot_2.wav", false, {"luger"}, -1, 1.04f, 0, 1.0f, 4.0f, 1.24f},   // B (committed: SND14)
    {"snd14/smg_shot_2.wav", false, {"modern_pistol2"}, -1, 1.00f, 0, 1.0f, 4.0f, 1.20f},   // SND17 (user request): the suppressed .22 and the Glock 43 trade shots, exactly
    {"guns/pistol_c.mp3", false, {"modern_pistol3"}},   // (the .22's set; the swap leaves the suppressed gun the quieter by 4 dB, so no re-level)
    {"snd14/smg_shot_2.wav", false, {"modern_pistol4"}, -1, 0.95f, 0, 1.0f, 4.0f, 1.20f},
    {"snd15/pistol_3.wav", false, {"m1911a1"}, -1, 1.00f, 0, 1.2f, 1.5f, 1.22f},   // C (.45)
    {"snd15/pistol_3.wav", false, {"modern_mark23"}, -1, 0.95f, 0, 1.2f, 1.5f, 1.20f},
    {"snd15/pistol_4.wav", false, {"modern_fn502"}, -1, 1.00f, 0, 1.0f, 0.0f, 0.83f},   // D (.22)
    {"snd15/pistol_4.wav", false, {"modern_scorpion"}, -1, 1.15f, 0, 1.0f, 0.0f, 1.16f},   // D higher: the Scorpion (its old recording went to the AKs)
    {"snd15/shotgun.wav", false, {"modern_shotgun"}, -1, 1.00f, 0, 2.4f, 0.0f, 0.53f},
    {"snd15/shotgun.wav", false, {"modern_shotgun2"}, -1, 0.96f, 0, 2.4f, 0.0f, 0.54f},
    {"snd15/shotgun.wav", false, {"modern_sawnoff"}, -1, 0.92f, 0, 2.4f, 0.0f, 0.55f},
    {"snd15/suppressed_rifle.wav", false, {"modern_mp7"}, -1, 1.05f, 0, 1.4f, 1.0f, 0.99f},
    {"snd15/suppressed_rifle.wav", false, {"modern_vsk"}, -1, 0.92f, 0, 1.4f, 2.5f, 1.14f},
    {"snd15/suppressed_rifle.wav", false, {"modern_crossbow"}, -1, 0.70f, -0.4f, 0.5f, 0.0f, 0.55f},   // XB1 stand-in (the performance session owns sound): the suppressed rifle much lower, darker, short and quiet: a thump
    {"snd15/suppressed_rifle.wav", false, {"suppressed_rifle"}, -1, 1.00f, 0, 1.4f, 0.0f, 1.18f},   // a fitted suppressor: 6 dB under the rifle class
    {"snd14/bolt_rifle.wav", false, {"kar98k"}, -1, 1.00f, 0, 1.8f, 4.5f, 1.23f},   // BoltActionRifle: the WW2 bolt guns (the modern snipers keep the M24 / .50)
    {"snd14/bolt_rifle.wav", false, {"kar98k_zf39"}, -1, 0.96f, 0, 1.8f, 4.0f, 1.23f},
    // SND16 (user request; local-only content/audio/custom/snd16/, licences not stated): the MG 42 (carried and emplaced) on 13 single
    // rounds cut from one clean burst (freesound community), the M249 on 13 from its own burst ("m249 sound.mp3"; E:/nmk/snd16/prep16.py), each its round up to
    // the next and then the burst's last ring-out, so automatic fire (each body cut at the next round) varies round to round and the
    // last shot rings out; levelled on sustained fire at each gun's rate to the old sets (a lone round is drier than the old shots)
    {"snd16/mg_shot_%d.wav", false, {"mg42"}, -1, 0.95f, 0, 0.3f, 3.0f, 1.11f, 13},   // 7.92 mm: a little lower, 1200 rpm
    {"snd16/m249_shot_%d.wav", false, {"modern_lmg"}, -1, 1.00f, 0, 0.6f, 4.0f, 1.23f, 13},   // 800 rpm (its recording ~750); replaces SND15's Scorpion recording, the fallback
    // SND18 (user request; content/audio/custom/snd18/, supplied by the project owner, stated free to use; tools/snd18_prep.py, tools/snd18_gen.py):
    // the per-gun pack replaces every set above for the guns it covers. <gun>_single: the single shot, lead-in trimmed; a gun that
    // fires automatic plays it as "shot_<id>_semi" in SEMI (weapons.cpp / game.cpp) and the distinct rounds of its 5-round burst
    // (each up to the next round, then the single's ring-out) as "shot_<id>", so vehicles, mounts and LAN echoes get them too.
    // Levelled by class on the first 150 ms (pistols -5 dB, SMGs -3, rifles / shotguns 0, MGs +1.5, .50 / 30 mm +3; gain capped at
    // 2.2, the MG bursts 1 dB short); no body. Shared recordings pitch-stepped: glock = beretta (Glock 43 1.00, Glock 34 1.05,
    // service pistol 0.94), bar_single = fg42_single (BAR 0.97, FG 42 1.03; their bursts differ)
    {"snd18/luger_single.mp3", false, {"luger"}, -1, 1.00f, 0, 1.6f, 0.0f, 0.84f},
    {"snd18/m1911_single.mp3", false, {"m1911a1"}, -1, 1.00f, 0, 1.6f, 0.0f, 0.64f},
    {"snd18/makarov_single.mp3", false, {"modern_makarov"}, -1, 1.00f, 0, 1.6f, 0.0f, 0.80f},
    {"snd18/hk_mk23_single.mp3", false, {"modern_mark23"}, -1, 1.00f, 0, 1.6f, 0.0f, 0.98f},
    {"snd18/deagle_single.mp3", false, {"modern_deagle"}, -1, 1.00f, 0, 1.6f, 0.0f, 0.85f},
    {"snd18/glock_single.mp3", false, {"modern_pistol3"}, -1, 1.00f, 0, 1.6f, 0.0f, 1.50f},
    {"snd18/glock_single.mp3", false, {"modern_glock34"}, -1, 1.05f, 0, 1.6f, 0.0f, 1.50f},
    {"snd18/beretta_single.mp3", false, {"modern_pistol4"}, -1, 0.94f, 0, 1.6f, 0.0f, 1.50f},
    {"snd18/mp40_single.mp3", false, {"mp40_semi"}, -1, 1.00f, 0, 1.4f, 0.0f, 1.34f},
    {"snd18/mp40_auto_%d.mp3", false, {"mp40"}, -1, 1.00f, 0, 1.4f, 0.0f, 1.51f, 5},
    {"snd18/thompson_single.mp3", false, {"m1_thompson_semi"}, -1, 1.00f, 0, 1.4f, 0.0f, 1.52f},
    {"snd18/thompson_auto_%d.mp3", false, {"m1_thompson"}, -1, 1.00f, 0, 1.4f, 0.0f, 1.69f, 4},
    {"snd18/m3_grease_gun_single.mp3", false, {"m3_grease_gun_semi"}, -1, 1.00f, 0, 1.4f, 0.0f, 2.00f},
    {"snd18/m3_grease_gun_auto_%d.mp3", false, {"m3_grease_gun"}, -1, 1.00f, 0, 1.4f, 0.0f, 1.97f, 4},
    {"snd18/sten_single.mp3", false, {"sten_semi"}, -1, 1.00f, 0, 1.4f, 0.0f, 1.62f},
    {"snd18/sten_auto_%d.mp3", false, {"sten"}, -1, 1.00f, 0, 1.4f, 0.0f, 1.59f, 4},
    {"snd18/cz_scorpion_single.mp3", false, {"modern_scorpion_semi"}, -1, 1.00f, 0, 1.4f, 0.0f, 1.49f},
    {"snd18/cz_scorpion_auto_%d.mp3", false, {"modern_scorpion"}, -1, 1.00f, 0, 1.4f, 0.0f, 1.64f, 4},
    {"snd18/hk_mp7_single.mp3", false, {"modern_mp7_semi"}, -1, 1.00f, 0, 1.4f, 0.0f, 1.81f},
    {"snd18/hk_mp7_auto_%d.mp3", false, {"modern_mp7"}, -1, 1.00f, 0, 1.4f, 0.0f, 1.84f, 5},
    {"snd18/ump_single.mp3", false, {"modern_smg45_semi"}, -1, 1.00f, 0, 1.4f, 0.0f, 2.01f},
    {"snd18/ump_auto_%d.mp3", false, {"modern_smg45"}, -1, 1.00f, 0, 1.4f, 0.0f, 1.97f, 3},
    {"snd18/uzi_single.mp3", false, {"modern_uzi_semi"}, -1, 1.00f, 0, 1.4f, 0.0f, 1.81f},
    {"snd18/uzi_auto_%d.mp3", false, {"modern_uzi"}, -1, 1.00f, 0, 1.4f, 0.0f, 1.83f, 4},
    {"snd18/m1_carbine_single.mp3", false, {"m1_carbine"}, -1, 1.00f, 0, 2.0f, 0.0f, 0.97f},
    {"snd18/m1_garand_single.mp3", false, {"m1_garand"}, -1, 1.00f, 0, 2.0f, 0.0f, 0.93f},
    {"snd18/m1_garand_single.mp3", false, {"m1c_garand"}, -1, 0.97f, 0, 2.0f, 0.0f, 0.93f},
    {"snd18/kar98k_single.mp3", false, {"kar98k"}, -1, 1.00f, 0, 2.5f, 0.0f, 1.48f},
    {"snd18/kar98k_single.mp3", false, {"kar98k_zf39"}, -1, 0.96f, 0, 2.5f, 0.0f, 1.48f},
    {"snd18/stg44_single.mp3", false, {"stg44_semi"}, -1, 1.00f, 0, 2.0f, 0.0f, 1.20f},
    {"snd18/stg44_auto_%d.mp3", false, {"stg44"}, -1, 1.00f, 0, 2.0f, 0.0f, 1.27f, 5},
    {"snd18/fg42_single.mp3", false, {"fg42_semi"}, -1, 1.03f, 0, 2.0f, 0.0f, 1.20f},
    {"snd18/fg42_auto_%d.mp3", false, {"fg42"}, -1, 1.03f, 0, 2.0f, 0.0f, 1.57f, 5},   // SND19: re-cut, re-levelled on sustained fire
    {"snd18/bar_single.mp3", false, {"m1918a2_bar_semi"}, -1, 0.97f, 0, 2.0f, 0.0f, 1.20f},
    {"snd18/bar_auto_%d.mp3", false, {"m1918a2_bar"}, -1, 0.97f, 0, 2.0f, 0.0f, 1.51f, 5},   // SND19: re-cut, re-levelled
    {"snd18/m4_single.mp3", false, {"modern_m4_irons_semi"}, -1, 1.00f, 0, 2.0f, 0.0f, 1.11f},
    {"snd18/m4_auto_%d.mp3", false, {"modern_m4_irons"}, -1, 1.00f, 0, 2.0f, 0.0f, 1.45f, 5},
    {"snd18/m16_single.mp3", false, {"modern_m16_semi"}, -1, 1.00f, 0, 2.0f, 0.0f, 1.22f},
    {"snd18/m16_auto_%d.mp3", false, {"modern_m16"}, -1, 1.00f, 0, 2.0f, 0.0f, 1.55f, 5},
    {"snd18/scar_h_single.mp3", false, {"modern_scar_semi"}, -1, 1.00f, 0, 2.0f, 0.0f, 1.46f},
    {"snd18/scar_h_auto_%d.mp3", false, {"modern_scar"}, -1, 1.00f, 0, 2.0f, 0.0f, 1.52f, 5},
    {"snd18/shotgun_remington870_single.mp3", false, {"modern_shotgun"}, -1, 1.00f, 0, 2.0f, 0.0f, 1.03f},
    {"snd18/shotgun_spas12_single.mp3", false, {"modern_shotgun2"}, -1, 1.00f, 0, 2.0f, 0.0f, 1.18f},
    {"snd18/mg42_auto_%d.mp3", false, {"mg42"}, -1, 1.00f, 0, 1.6f, 0.0f, 1.44f, 5},   // SND19: re-cut, re-levelled; the fallback (snd16 below)
    {"snd18/m249_auto_%d.mp3", false, {"modern_lmg"}, -1, 1.00f, 0, 1.6f, 0.0f, 1.50f, 5},   // SND19: re-cut, re-levelled; the fallback (snd16 below)
    {"snd18/m2_browning_single.mp3", false, {"m2hb_carry_semi"}, -1, 1.00f, 0, 1.6f, 0.0f, 2.04f},
    {"snd18/m2_browning_auto_%d.mp3", false, {"m2hb_carry"}, -1, 1.00f, 0, 1.6f, 0.0f, 2.08f, 5},
    {"snd18/m2_browning_single.mp3", false, {"m2hb_semi"}, -1, 1.00f, 0, 1.6f, 0.0f, 2.04f},
    {"snd18/m2_browning_auto_%d.mp3", false, {"m2hb"}, -1, 1.00f, 0, 1.6f, 0.0f, 2.08f, 5},
    {"snd18/30mm_cannon_auto_%d.mp3", false, {"cannon30"}, -1, 1.00f, 0, 1.6f, 0.0f, 1.95f, 5},   // SND19: re-cut, re-levelled to the M2
    // SWAP (user request: "suppressed 22 pistol and glock 43 should trade sounds"): the suppressed .22 takes the shot the Glock 43
    // had before SND18 (SND14's SMGShot1 at its settings); the Glock 43 plays the pack's glock file (above)
    {"snd14/smg_shot_2.wav", false, {"modern_pistol2"}, -1, 1.00f, 0, 1.0f, 4.0f, 1.20f},
    // SND19 (user: "the m249 sounds really weird"): the pack's bursts were cut into the single's ring-out at up to 4x its level, so on the
    // MG 42 / M249 the ring-out outweighed the round: the onset search started 2-3 of 5 variants inside it (no shot, 10 dB down: a
    // gallop), and every burst ended on a loud tail. Re-cut (the ring-out scaled by the round's attack; tools/snd18_prep.py) for the
    // five guns that had it (MG 42, M249, FG 42, BAR, 30 mm); the MG 42 and M249 go back to the user's own SND16 sets (local-only), auto
    // and semi alike (the pack singles are 10 dB hotter and another tone), the re-cut pack bursts their committed fallback
    {"snd16/mg_shot_%d.wav", false, {"mg42"}, -1, 0.95f, 0, 0.3f, 3.0f, 1.11f, 13},
    {"snd16/m249_shot_%d.wav", false, {"modern_lmg"}, -1, 1.00f, 0, 0.6f, 4.0f, 1.23f, 13},
    // SND20: the MP5 on the pack's mp40 set (an MP5 recording) 6 % higher than the MP 40; the AK-47 on its stg44 set (an AKM, 7.62x39)
    // 6 % lower than the StG 44; levelled on sustained fire (SMG -8.5, rifle -7.0 dB), no late-cut variant at 800 / 600 rpm
    {"snd18/mp40_single.mp3", false, {"modern_mp5_semi"}, -1, 1.06f, 0, 1.4f, 0.0f, 1.22f},
    {"snd18/mp40_auto_%d.mp3", false, {"modern_mp5"}, -1, 1.06f, 0, 1.4f, 0.0f, 1.38f, 5},
    {"snd18/stg44_single.mp3", false, {"modern_ak47_semi"}, -1, 0.94f, 0, 2.0f, 0.0f, 1.29f},
    {"snd18/stg44_auto_%d.mp3", false, {"modern_ak47"}, -1, 0.94f, 0, 2.0f, 0.0f, 1.36f, 5},
  };
  std::deque<std::string> varFile; std::vector<std::pair<GunSet, int>> sets;   // SND16: the variant sets expanded, second = its variant group (0: none)
  for (auto& g : guns) { if (g.vars <= 0) { sets.push_back({g, 0}); continue; }
    for (int k = 1; k <= g.vars; ++k) { char nb[160]; std::snprintf(nb, sizeof nb, g.file, k); GunSet h = g; h.file = varFile.emplace_back(nb).c_str(); sets.push_back({h, (int)(&g - guns) + 1}); } }
  std::set<std::string> varStarted;   // a variant group's first decoded file replaces the old event, the rest add to it (a missing file: the old set stays); SND18: per group, so a later group replaces an earlier one
  // SND5: each cut, resample and tone step runs on the mono mix and on both channels alike; levels: quieter recordings up to
  // 0.95 peak (the stereo pair by its louder channel), full-scale ones unchanged; the cut starts where the shot rises out of the quiet before its onset (no fade-in)
  Buf gL, gR; int n18 = 0;
  for (auto& [g, isVar] : sets) {
    if (!decodeSt(g.file, b, &gL, &gR)) continue;
    n18 += std::strncmp(g.file, "snd18/", 6) == 0;
    const int W = SR / 200; size_t nW = b.size() / W; float peak = 1e-4f; std::vector<float> env(nW, 0.0f);   // 5 ms windows
    for (size_t k = 0; k < nW; ++k) { for (int j = 0; j < W; ++j) env[k] = std::max(env[k], std::fabs(b[k * W + j])); peak = std::max(peak, env[k]); }
    size_t first = nW, last = nW, prev = 0; std::vector<size_t> shots;   // shots: onsets at least 1 s apart (a recording of several)
    for (size_t k = 0; k < nW; ++k) if (env[k] > 0.5f * peak && (k == 0 || env[k - 1] <= 0.5f * peak) && (last == nW || k - prev >= 8)) { if (first == nW) first = k; last = prev = k; if (shots.empty() || k - shots.back() >= 200) shots.push_back(k); }
    if (first == nW) continue;
    size_t a = g.take >= 0 && g.take < (int)shots.size() ? shots[g.take] : (g.burst ? last : first);
    for (size_t back = 0; a > 0 && back < 2 && env[a - 1] > 0.02f * peak; ++back) --a;   // up to 10 ms of the rise (SND8: was 50 ms, which ate most of an automatic round)
    a = (a > 0 ? a - 1 : 0) * W;
    size_t e = std::min(b.size(), a + (size_t)(g.tail * SR));
    if (g.take >= 0 && g.take + 1 < (int)shots.size()) e = std::min(e, (shots[g.take + 1] - 2) * W);   // not into the next shot
    { static const bool lg = std::getenv("NMK_AUDIO_LOG") != nullptr; if (lg && g.take >= 0) logf("audio: %s take %d of %d shots, from %.2f s", g.file, g.take, (int)shots.size(), a / (float)SR); }   // diagnostics
    auto shape = [&](const Buf& src) {
      Buf s(src.begin() + a, src.begin() + e);
      if (g.pitch != 1.0f) s = resampleSinc(s, 1.0 / g.pitch);   // SND4 derived: pitched (SND5: band-limited)
      if (g.tilt != 0.0f) { float lp = 0, al = 1 - std::exp(-2 * 3.14159f * 1500.0f / SR); for (float& x : s) { lp += al * (x - lp); x = g.tilt < 0 ? x * (1 + g.tilt) - lp * g.tilt : x + (x - lp) * g.tilt; } }   // tone: toward the low-passed (< 0) or the high-passed (> 0)
      size_t f = std::min(s.size(), (size_t)(0.05f * SR));
      for (size_t j = s.size() > f ? s.size() - f : 0; j < s.size(); ++j) s[j] *= (float)(s.size() - j) / f;   // 50 ms fade-out
      return s;
    };
    Buf s = shape(b), sl = shape(gL), sr = shape(gR);
    float body = g.body; if (g.gain < 0) for (const char* id : g.ids) if (mixGuns.contains(id) && mixGuns[id].is_object()) body = std::clamp(mixGuns[id].value("body", body), 0.0f, 6.0f);   // SND7: mix.json override
    if (body > 0) for (Buf* c : {&s, &sl, &sr}) {   // SND5: +low-mid (a 450 Hz low-pass mixed in), then saturation (drive g.body): denser at the same peak
      float lp = 0, al = 1 - std::exp(-2 * 3.14159f * 450.0f / SR), pk = 1e-4f; for (float& x : *c) { lp += al * (x - lp); x += 0.8f * lp; pk = std::max(pk, std::fabs(x)); }
      for (float& x : *c) x = std::tanh(body * x / pk) / std::tanh(body);
    }
    float pk = 1e-4f; for (float x : s) pk = std::max(pk, std::fabs(x)); for (float& x : s) x *= std::max(1.0f, 0.95f / pk);
    float pk2 = 1e-4f; for (size_t j = 0; j < sl.size(); ++j) pk2 = std::max({pk2, std::fabs(sl[j]), std::fabs(sr[j])});
    const float g2 = std::max(1.0f, 0.95f / pk2);   // a full-scale recording as it is (the limiter takes its mp3 overshoot), a quieter one up to 0.95
    Buf st(sl.size() * 2); for (size_t j = 0; j < sl.size(); ++j) { st[2 * j] = sl[j] * g2; st[2 * j + 1] = sr[j] * g2; }
    ++used; Impl::Event ev; ev.kind = "shot"; ev.v.push_back(std::move(s)); ev.st.push_back(std::move(st));
    for (const char* id : g.ids) {
      if (isVar && !varStarted.insert(std::to_string(isVar) + id).second) { Impl::Event& ex = p->events[std::string("shot_") + id]; ex.v.push_back(ev.v[0]); ex.st.push_back(ev.st[0]); continue; }   // SND16: another variant
      Impl::Event& e2 = p->events[std::string("shot_") + id] = ev; p->events.erase(std::string("shot_") + id + "_far");
      const bool mini = std::string(id) == "minigun" || std::string(id) == "modern_minigun", c30 = std::string(id) == "cannon30";
      if (mini || c30) { e2.minGap = mini ? 0.04f : 0.045f; e2.maxVoices = mini ? 6 : 4; e2.rolloff = 0.8f; e2.gain = mini ? 0.7f : 0.85f; }   // SND9: rapid fire, carries far
      if (g.gain >= 0) e2.gain = g.gain;   // SND14: the set's own level
      else if (mixGuns.contains(id)) { const json& m = mixGuns[id]; e2.gain = std::clamp(m.is_number() ? m.get<float>() : m.is_object() ? m.value("gain", e2.gain) : e2.gain, 0.0f, 2.0f);   // SND7: per-gun trim
        if (m.is_object()) { e2.minGap = std::clamp(m.value("min_gap_ms", e2.minGap * 1000) / 1000.0f, 0.0f, 0.5f); e2.maxVoices = std::clamp(m.value("max_voices", e2.maxVoices), 1, 16); if (m.contains("rolloff")) e2.rolloff = std::clamp(m.value("rolloff", 1.2f), 0.1f, 2.0f); } }
    }
  }
  if (n18) logf("audio: SND18 pack: %d shot files (shot_mg42 %zu variants, shot_mg42_semi %zu, shot_modern_mp5 %zu, shot_modern_ak47 %zu)", n18, p->events.count("shot_mg42") ? p->events["shot_mg42"].v.size() : 0,
                p->events.count("shot_mg42_semi") ? p->events["shot_mg42_semi"].v.size() : 0, p->events.count("shot_modern_mp5") ? p->events["shot_modern_mp5"].v.size() : 0,
                p->events.count("shot_modern_ak47") ? p->events["shot_modern_ak47"].v.size() : 0);
  // SND4: the bolt action cycle after each shot of a bolt rifle: the recording's open / close (0.36-0.90 s) twice back to back,
  // sped up (resampled) so both, from 0.06 s after the shot, end 0.05 s before the bolt time is up (at least 1.25x)
  // SND17 (user request): the user's bolt recording (local-only content/audio/custom/snd17/bolt_cycle.wav: E:/nmk/refs/bolt_user.mp3
  // trimmed offline (the weapons session, bf30536f) to its one full cycle, lift / back / forward / close, 0.70-1.48 s) played once, sped up only as far as the bolt
  // time needs (Kar98k 1.05x, the others 1.0x), at the old pair's 0.8 peak; without it the old recording's slice twice, as before
  const bool one = decode("snd17/bolt_cycle.wav", b) && b.size() > (size_t)(0.2f * SR);
  if (one || (decode("guns/bolt_cycle.mp3", b) && b.size() > (size_t)(0.90f * SR))) {
    Buf seg = one ? b : Buf(b.begin() + (size_t)(0.36f * SR), b.begin() + (size_t)(0.90f * SR)); float pk = 1e-4f; for (float x : seg) pk = std::max(pk, std::fabs(x)); for (float& x : seg) x *= 0.8f / pk;
    const int reps = one ? 1 : 2;
    struct BoltSet { const char* id; float bolt; };
    for (auto& bs : {BoltSet{"kar98k", 0.85f}, BoltSet{"kar98k_zf39", 1.0f}, BoltSet{"modern_sniper_bolt", 1.0f}, BoltSet{"modern_sniper_3", 1.0f}, BoltSet{"modern_sniper_50", 1.4f}}) {   // content/weapons boltTime
      const float lead = 0.06f, len = seg.size() / (float)SR, speed = std::max(one ? 1.0f : 1.25f, reps * len / (bs.bolt - 0.05f - lead));
      Buf r((size_t)(seg.size() / speed)); for (size_t j = 0; j < r.size(); ++j) { float x = j * speed; size_t i0 = (size_t)x; float u = x - i0; r[j] = i0 + 1 < seg.size() ? seg[i0] * (1 - u) + seg[i0 + 1] * u : seg.back(); }
      Buf out((size_t)(lead * SR), 0.0f); for (int q = 0; q < reps; ++q) out.insert(out.end(), r.begin(), r.end());
      size_t f = std::min(out.size(), (size_t)(SR / 100)); for (size_t j = 0; j < f; ++j) out[out.size() - 1 - j] *= (float)j / f;
      Impl::Event ev; ev.gain = 0.8f; ev.v.push_back(std::move(out)); p->events[std::string("bolt_cycle_") + bs.id] = ev;
      { static const bool lg = std::getenv("NMK_AUDIO_LOG") != nullptr; if (lg) logf("audio: bolt cycle for %s: %.2fx, ends %.2f s after the shot (bolt %.2f s)", bs.id, speed, lead + reps * len / speed, bs.bolt); }   // diagnostics
    }
    ++used; logf("audio: bolt cycle: %s", one ? "SND17 recording, once" : "old recording, twice");
  }
  // SND4: tank main guns (every tank: tank_sim plays tank_88 for the Tiger, tank_17pdr otherwise; both were procedural)
  // SND9: big one-shot recordings (the tank gun, the shell hit): from the onset, low-mid body and saturation (drive), levelled
  // to 0.97 peak, mono for the distance and stereo for the ear; mix.json "<key>": gain, body, full_level_m, rolloff
  auto bigShot = [&](const char* file, const char* key, float secs, float body0, float full0, float roll0, float ear, std::vector<const char*> names) {
    Buf L, R; if (!decodeSt(file, b, &L, &R)) return;
    const json m = mx.contains(key) && mx[key].is_object() ? mx[key] : json::object();
    const float body = std::clamp(m.value("body", body0), 0.0f, 6.0f), gain = std::clamp(m.value("gain", 1.0f), 0.0f, 2.0f);
    float pk = 1e-4f; for (float x : b) pk = std::max(pk, std::fabs(x)); size_t a = 0; while (a < b.size() && std::fabs(b[a]) < 0.2f * pk) ++a; a = a > (size_t)(SR / 100) ? a - SR / 100 : 0;
    auto shape = [&](const Buf& src) {
      Buf s(src.begin() + std::min(a, src.size()), src.begin() + std::min(src.size(), a + (size_t)(secs * SR)));
      if (body > 0) { float lp = 0, al = 1 - std::exp(-2 * 3.14159f * 450.0f / SR), q = 1e-4f; for (float& x : s) { lp += al * (x - lp); x += 0.8f * lp; q = std::max(q, std::fabs(x)); } for (float& x : s) x = std::tanh(body * x / q) / std::tanh(body); }
      size_t f = std::min(s.size(), (size_t)(0.3f * SR)); for (size_t j = s.size() - f; j < s.size(); ++j) s[j] *= (float)(s.size() - j) / f;
      return s;
    };
    Buf s = shape(b), sl = shape(L), sr = shape(R);
    float q = 1e-4f; for (float x : s) q = std::max(q, std::fabs(x)); for (float& x : s) x *= 0.97f / q;
    float q2 = 1e-4f; for (size_t j = 0; j < sl.size(); ++j) q2 = std::max({q2, std::fabs(sl[j]), std::fabs(sr[j])});
    Buf st(sl.size() * 2); for (size_t j = 0; j < sl.size(); ++j) { st[2 * j] = sl[j] * 0.97f / q2; st[2 * j + 1] = sr[j] * 0.97f / q2; }
    Impl::Event ev; ev.minDist = std::clamp(m.value("full_level_m", full0), 1.0f, 200.0f); ev.rolloff = std::clamp(m.value("rolloff", roll0), 0.1f, 2.0f);
    ev.gain = gain; ev.bus = 4; ev.earM = ear; ev.duck = true; ev.v.push_back(std::move(s)); ev.st.push_back(std::move(st));
    for (const char* n : names) { p->events[n] = ev; p->events.erase(std::string(n) + "_far"); }
    ++used;
  };
  // SND4 / SND9: tank main guns (every tank: tank_sim plays tank_88 for the Tiger, tank_17pdr otherwise): full level to 15 m,
  // slow roll-off (audible far), the stereo recording when the listener is within 8 m (in or on the tank)
  bigShot("guns/tank_shot.mp3", "tank_gun", 5.0f, 3.0f, 15.0f, 0.5f, 8.0f, {"tank_88", "tank_17pdr"});
  // SND9: a tank / AT / cannon shell bursting (station.cpp): the heaviest supplied explosion (launcher_fire.mp3, big-explosion-sfx)
  bigShot("guns/launcher_fire.mp3", "shell_hit", 6.0f, 2.0f, 25.0f, 0.6f, 0.0f, {"shell_hit"});
  // launchers, cannon and explosions: whole recordings from their first loud moment (up to 6 s), far layers dropped
  struct FxSet { const char* file; std::vector<const char*> events; float from = 0; };   // from: seconds into the recording to start at
  const FxSet fxs[] = {
    {"guns/rocket_launch.mp3", {"shot_bazooka_m9", "shot_panzerschreck", "shot_modern_rpg7", "shot_rocket_pod", "shot_tow"}, 3.0f},
    {"snd14/rocket_launch.wav", {"shot_bazooka_m9", "shot_panzerschreck", "shot_modern_rpg7", "shot_rocket_pod", "shot_tow"}},   // SND14 (user request): RocketLaunch, every launcher (and the heli pods / TOW, which play these); local-only, the old one without it
    {"snd18/bazooka_single.mp3", {"shot_bazooka_m9"}}, {"snd18/panzerschreck_single.mp3", {"shot_panzerschreck"}}, {"snd18/rpg7_single.mp3", {"shot_modern_rpg7"}},   // SND18: the three launchers' own (the heli pods / TOW keep RocketLaunch)
    {"guns/glauncher_fire.mp3", {"shot_modern_glauncher"}},   // SND4: the six-shot launcher's own
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
  // RM1: the heavies' stomp / slam (stomp_area: the golem, the abomination); an older install's copy keeps its old file name
  if (decode("heavy_stomp.mp3", b) || decode("fighter_stomp.mp3", b)) { ++used; Impl::Event ev; ev.gain = 1.5f; ev.minDist = 60.0f; ev.v.push_back(b); p->events["heavy_stomp"] = ev; }
  { Impl::Event ev; for (const char* f : {"spider_attack_1.mp3", "spider_attack_2.mp3", "spider_attack_3.mp3"}) if (decode(f, b)) ev.v.push_back(b);   // the spider's strike: a random one each time
    if (!ev.v.empty()) { ++used; p->events["spider_attack"] = ev; } }
  for (const char* n : {"troll_attack", "troll_idle"}) if (decode((std::string(n) + ".mp3").c_str(), b)) { ++used; if (b.size() > (size_t)(5 * SR)) { b.resize(5 * SR); for (int j = 0; j < SR / 5; ++j) b[b.size() - 1 - j] *= j / (SR / 5.0f); } Impl::Event ev; ev.v.push_back(b); p->events[n] = ev; }
  {   // reload recordings (reload/*.mp3): one whole reload per magazine / clip / belt weapon at reload start; shotguns: the pump after each
      // shot and one mid-track shell-loading sound per shell (also the single-round loading sound of other guns)
    // SND3: secs: the group's shortest (partial) reload time; the recording is fitted to it (fitTo); keep: seconds of the
    // file used (the AK recording's loud second half is dropped)
    struct RSet { const char* file; std::vector<const char*> ids; float secs = 6.0f, keep = 0, pitch = 1; };   // MG12 pitch: a derived variant (resampled), as the gun shots
    const RSet rs[] = {
      {"reload/reload_rifle.mp3", {"mg42", "m2hb_carry"}},   // belt guns keep the generic rifle recording
      {"reload/reload_ak.mp3", {"modern_ak", "modern_ak12"}, 2.4f, 3.5f},   // rock-in magazines (jigokukarano_sisya)
      {"reload/reload_ak.mp3", {"stg44"}, 2.7f, 3.5f},
      {"reload/reload_ak.mp3", {"modern_ak47"}, 2.5f, 3.5f, 0.96f},   // SND20: a little lower than the AK-74M's
      {"reload/reload_m4.mp3", {"modern_m4_irons"}, 2.2f},   // AR pattern and the other magazine rifles (squarebun)
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
      {"reload/reload_a.mp3", {"mp40", "m1_thompson", "m3_grease_gun", "sten", "modern_uzi", "modern_mp7", "modern_scorpion", "m1_garand", "m1c_garand", "kar98k_zf39"}},
      {"reload/reload_b.mp3", {"luger", "m1911a1", "modern_pistol", "modern_pistol2", "modern_pistol3"}},
      // MG12 (borrowed, pitched apart): the Desert Eagle on the pistol reload, lower; the SMG-9 on the SMG reload, higher; the
      // sawn-off's two shells on the shell recording (the revolver loads round by round: clip_in per round)
      {"reload/reload_b.mp3", {"modern_deagle"}, 1.8f, 0, 0.9f},
      {"reload/reload_a.mp3", {"modern_smg9"}, 1.8f, 0, 1.06f},
      {"reload/reload_a.mp3", {"modern_mp5"}, 2.4f, 0, 0.97f},   // SND20
      {"reload/shotgun_shells.mp3", {"modern_sawnoff"}, 2.9f, 0, 1.05f},
      {"reload/reload_b.mp3", {"modern_crossbow"}, 2.5f, 0, 0.8f},
      {"reload/reload_a.mp3", {"modern_minigun"}, 4.3f, 0, 0.85f},   // MG13 stand-in: the box swap   // XB1 stand-in: cocking and laying the bolt
      // SW1 (borrowed, pitched apart): the energy-cell swaps on the AR magazine reload
      {"reload/reload_m4.mp3", {"blaster_carbine"}, 1.9f, 0, 1.12f},
      // MG12 batch 2 (borrowed, pitched apart): the pistol reload
      {"reload/reload_b.mp3", {"modern_glock34"}, 1.9f, 0, 1.08f},
      {"reload/reload_b.mp3", {"modern_mark23"}, 3.0f, 0, 0.85f},
      {"reload/reload_ak.mp3", {"modern_vsk"}, 2.5f, 3.5f, 1.08f},
      {"reload/reload_m4.mp3", {"blaster_rifle"}, 2.4f, 0, 0.92f},
      {"reload/reload_m4.mp3", {"blaster_robot"}, 2.1f, 0, 1.2f},
      {"reload/reload_b.mp3", {"modern_makarov"}, 2.2f, 0, 0.95f},   // NG1: back (MG12's)
      {"reload/reload_b.mp3", {"modern_fn502"}, 2.0f, 0, 1.12f},   // NG1
      {"reload/reload_b.mp3", {"modern_pistol4"}, 2.0f, 0, 1.0f},
      {"reload/reload_a.mp3", {"modern_smg45"}, 2.2f, 0, 0.94f},
      {"snd14/mg_reload.wav", {"mg42"}, 4.6f},   // SND14 (user request): machinegunreload on the belt guns' box / belt change (local-only; the rifle recording without it)
      {"snd14/mg_reload.wav", {"modern_lmg"}, 5.2f},
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
      if (g.pitch != 1.0f) b = resampleSinc(b, 1.0 / g.pitch);
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
  // SND4: the strongest `secs` of a recording (10 ms energy), from sample `from` on: its first sample
  auto strongest = [&](const Buf& s, float secs, size_t from = 0) {
    const int W = SR / 100; size_t nW = s.size() / W, len = std::max<size_t>(1, std::min(nW, (size_t)(secs * 100))), best = from / W; double run = 0, bs = -1;
    std::vector<double> e(nW, 0.0); for (size_t k = 0; k < nW; ++k) for (int j = 0; j < W; ++j) e[k] += s[k * W + j] * s[k * W + j];
    for (size_t k = from / W; k < nW; ++k) { run += e[k]; if (k >= from / W + len) run -= e[k - len]; if (k + 1 >= from / W + len && run > bs) { bs = run; best = k + 1 - len; } }
    return best * W;
  };
  auto clip = [&](const Buf& s, size_t a, size_t n, float fadeIn, float fadeOut, float level) {   // a cut with linear fades, levelled to its peak
    Buf c(s.begin() + std::min(a, s.size()), s.begin() + std::min(s.size(), a + n)); float pk = peakOf(c);
    float fi = std::max(1.0f, fadeIn * SR), fo = std::max(1.0f, fadeOut * SR);
    for (size_t j = 0; j < c.size(); ++j) c[j] *= level / pk * std::min({1.0f, j / fi, (c.size() - j) / fo});
    return c;
  };
  // SND4: the creature's pre-attack screech (zombie.cpp "windup"): the strongest 1.2 s of the recording
  if (decode("creature_screech.mp3", b)) { Impl::Event ev; ev.minDist = 4.0f; ev.v.push_back(clip(b, strongest(b, 1.2f), (size_t)(1.2f * SR), 0.01f, 0.1f, 0.9f)); p->events["creature_screech"] = ev; ++used; }
  // SND4: the big monsters' voice (monster_growl.mp3): its loud stretches (100 ms loudness over 0.3 of the loudest, gaps
  // under 0.25 s bridged, at least 0.8 s, at most 3.5 s each) as idle growls, and its three strongest 1.5 s as attack roars
  if (decode("monster_growl.mp3", b)) {
    const int W = SR / 10; size_t nW = b.size() / W; std::vector<float> rms(nW, 0.0f); float top = 1e-6f;
    for (size_t k = 0; k < nW; ++k) { double q = 0; for (int j = 0; j < W; ++j) q += b[k * W + j] * b[k * W + j]; rms[k] = (float)std::sqrt(q / W); top = std::max(top, rms[k]); }
    std::vector<std::pair<size_t, size_t>> runs;
    for (size_t k = 0; k < nW; ++k) if (rms[k] > 0.3f * top) { if (!runs.empty() && k - runs.back().second <= 3) runs.back().second = k; else runs.push_back({k, k}); }
    Impl::Event idle; idle.minDist = 6.0f;
    for (auto& r : runs) if ((r.second - r.first + 1) >= 8 && idle.v.size() < 8) idle.v.push_back(clip(b, r.first * W, std::min<size_t>(35, r.second - r.first + 1) * W, 0.02f, 0.15f, 0.8f));
    Impl::Event atk; atk.minDist = 6.0f; Buf m = b;
    for (int i = 0; i < 3; ++i) { size_t a = strongest(m, 1.5f); atk.v.push_back(clip(b, a, (size_t)(1.5f * SR), 0.01f, 0.12f, 0.9f)); std::fill(m.begin() + a, m.begin() + std::min(m.size(), a + (size_t)(1.5f * SR)), 0.0f); }
    if (!idle.v.empty()) p->events["monster_growl"] = idle;
    p->events["monster_growl_attack"] = atk; ++used;
    { static const bool lg = std::getenv("NMK_AUDIO_LOG") != nullptr; if (lg) logf("audio: monster_growl %d idle growls, 3 attack roars", (int)idle.v.size()); }   // diagnostics
  }
  // SND4: a spent case hitting the ground (guns/casings.mp3: one landing with its bounces), 0.4 s from its onset
  {   // SND6: mix.json: casing_gain (x on the spent-case sound; 1 = the SND4 level)
    if (mx.is_object()) p->casingGain = std::clamp(mx.value("casing_gain", p->casingGain), 0.0f, 3.0f);
    if (mx.is_object() && mx.contains("reverb") && mx["reverb"].is_object() && p->verbOk) {   // per class: send (0-1) and tail_s (0.3-4 s)
      const char* cls[4] = {"gun", "mg", "fifty", "tank"};
      for (int k = 0; k < 4; ++k) if (mx["reverb"].contains(cls[k]) && mx["reverb"][cls[k]].is_object()) {
        const json& r = mx["reverb"][cls[k]]; VerbNode& v = p->verb[k];
        v.send = std::clamp(r.value("send", v.send), 0.0f, 1.0f); v.rtOut = std::clamp(r.value("tail_s", v.rtOut), 0.3f, 4.0f); v.setRoom(v.room < 0 ? 0 : v.room, true);
      }
    }
  }
  if (decode("guns/casings.mp3", b)) {
    float pk = peakOf(b); size_t a = 0; while (a < b.size() && std::fabs(b[a]) < 0.3f * pk) ++a; a = a > (size_t)(SR / 100) ? a - SR / 100 : 0;
    Impl::Event ev; ev.minDist = 2.0f; ev.v.push_back(clip(b, a, (size_t)(0.4f * SR), 0.002f, 0.08f, 0.8f)); p->events["casing"] = ev; ++used;
  }
  // SND4: the explosion pack's countdown (fx/bomb_beeps.mp3): its beeps by onset (5 ms loudness over 0.4 of the loudest,
  // gaps under 15 ms bridged): the first long one (>= 90 ms) and the first short one; the shortest is the short one's first
  // 25 ms (the recording has two lengths). abilities.cpp plays them
  if (decode("fx/blaster_hit.wav", b)) { ++used; Impl::Event ev; ev.v.push_back(b); p->events["blaster_hit"] = ev; }   // SW1: a bolt striking (synthesised sizzle)
  if (decode("fx/bomb_beeps.mp3", b)) {
    const int W = SR / 200; size_t nW = b.size() / W; std::vector<float> env(nW, 0.0f); float pk = 1e-6f;
    for (size_t k = 0; k < nW; ++k) { for (int j = 0; j < W; ++j) env[k] = std::max(env[k], std::fabs(b[k * W + j])); pk = std::max(pk, env[k]); }
    std::vector<std::pair<size_t, size_t>> beeps;
    for (size_t k = 0; k < nW; ++k) if (env[k] > 0.4f * pk) { if (!beeps.empty() && k - beeps.back().second <= 3) beeps.back().second = k; else beeps.push_back({k, k}); }
    Buf L, S;
    for (auto& r : beeps) { size_t n = (r.second - r.first + 1) * W; Buf c = clip(b, r.first * W, n + W, 0.003f, 0.01f, 0.8f); if (n >= (size_t)(0.09f * SR)) { if (L.empty()) L = c; } else if (n >= (size_t)(0.02f * SR) && S.empty()) S = c; }
    if (!L.empty()) {
      if (S.empty()) S = clip(L, 0, (size_t)(0.05f * SR), 0.003f, 0.01f, 0.8f);   // one length only: a trimmed long beep
      Buf T = clip(S, 0, (size_t)(0.025f * SR), 0.002f, 0.008f, 0.8f);
      for (auto& [n, s] : {std::pair<const char*, Buf*>{"pack_beep_long", &L}, {"pack_beep_short", &S}, {"pack_beep_shortest", &T}}) { Impl::Event ev; ev.minDist = 4.0f; ev.v.push_back(*s); p->events[n] = ev; }
      ++used;
      { static const bool lg = std::getenv("NMK_AUDIO_LOG") != nullptr; if (lg) logf("audio: bomb beeps: %d found, long %.3f s, short %.3f s", (int)beeps.size(), L.size() / (float)SR, S.size() / (float)SR); }   // diagnostics
    }
  }
  // SND13: the low-health heartbeat: cut to whole beat cycles (a cycle starts at an onset after a gap of 0.42 s or more:
  // the lub-dub pair is closer), 8 ms fades at the seam; and the player's hurt grunts (one event; several grunts in the
  // file become its variants, played in turn with the event's random pick)
  heartbeatGain = std::clamp(mx.value("heartbeat", 1.0f), 0.0f, 4.0f); hurtGain = std::clamp(mx.value("hurt", 1.0f), 0.0f, 4.0f);
  musicGain = std::clamp(mx.value("music", 0.6f), 0.0f, 2.0f);
  if (!p->musicOn) { const fs::path mf = dir / "music" / "menu_title.mp3";   // SND21: local-only (Pixabay licence: not redistributed)
    if (fs::exists(mf) && ma_sound_init_from_file(&p->engine, mf.string().c_str(), MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, nullptr, &p->music) == MA_SUCCESS) {
      p->musicOn = true; p->route(p->music); ma_sound_set_looping(&p->music, MA_TRUE); ma_sound_set_volume(&p->music, musicGain); logf("audio: menu music %s (streamed, gain %.2f)", mf.filename().string().c_str(), musicGain);
    } else logf("audio: no menu music (%s)", mf.generic_string().c_str()); }
  if (decode("fx/heartbeat.mp3", b) && b.size() > (size_t)SR) {
    const int W = SR / 100; size_t nW = b.size() / W; std::vector<float> env(nW, 0.0f); float pk = 1e-6f;
    for (size_t k = 0; k < nW; ++k) { for (int j = 0; j < W; ++j) env[k] = std::max(env[k], std::fabs(b[k * W + j])); pk = std::max(pk, env[k]); }
    std::vector<size_t> on; for (size_t k = 1; k < nW; ++k) if (env[k] > 0.35f * pk && env[k - 1] <= 0.35f * pk && (on.empty() || k - on.back() > 8)) on.push_back(k);
    std::vector<size_t> cyc; for (size_t k = 0; k < on.size(); ++k) if (k == 0 || (on[k] - on[k - 1]) * W >= (size_t)(0.42f * SR)) cyc.push_back(on[k]);
    size_t a = 0, e = b.size();
    if (cyc.size() >= 3) { a = (cyc.front() > 2 ? cyc.front() - 2 : 0) * W; e = (cyc.back() - 2) * W; }   // first cycle .. the start of the last (whole cycles only)
    Buf s(b.begin() + a, b.begin() + e); const size_t f = std::min(s.size() / 4, (size_t)(0.008f * SR));
    for (size_t j = 0; j < f; ++j) { const float g = j / (float)f; s[j] *= g; s[s.size() - 1 - j] *= g; }
    for (float& x : s) x *= 0.8f / pk;
    p->hbBuf = std::move(s);
    ma_audio_buffer_config c = ma_audio_buffer_config_init(ma_format_f32, 1, p->hbBuf.size(), p->hbBuf.data(), nullptr); c.sampleRate = SR;
    if (ma_audio_buffer_init(&c, &p->hbAB) == MA_SUCCESS) {
      if (ma_sound_init_from_data_source(&p->engine, &p->hbAB, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &p->hb) == MA_SUCCESS) { p->hbOn = true; p->route(p->hb); ma_sound_set_looping(&p->hb, MA_TRUE); ma_sound_set_volume(&p->hb, 0); ++used; }
      else ma_audio_buffer_uninit(&p->hbAB);
    }
    logf("audio: heartbeat loop %.2f s, %zu beat cycles (%zu onsets)", p->hbBuf.size() / (float)SR, cyc.size() >= 3 ? cyc.size() - 1 : 0, on.size());
  }
  if (decode("fx/player_hurt.mp3", b)) {
    const int W = SR / 200; size_t nW = b.size() / W; std::vector<float> env(nW, 0.0f); float pk = 1e-6f;
    for (size_t k = 0; k < nW; ++k) { for (int j = 0; j < W; ++j) env[k] = std::max(env[k], std::fabs(b[k * W + j])); pk = std::max(pk, env[k]); }
    std::vector<std::pair<size_t, size_t>> seg;   // loud stretches (over 0.12 of the peak), gaps under 120 ms bridged
    for (size_t k = 0; k < nW; ++k) if (env[k] > 0.12f * pk) { if (!seg.empty() && k - seg.back().second <= 24) seg.back().second = k; else seg.push_back({k, k}); }
    Impl::Event ev;
    for (auto& r : seg) { if ((r.second - r.first + 1) * W < (size_t)(0.12f * SR)) continue; size_t a = r.first > 2 ? (r.first - 2) * W : 0; ev.v.push_back(clip(b, a, (r.second - r.first + 6) * W, 0.004f, 0.06f, 0.8f)); }
    if (ev.v.empty()) ev.v.push_back(clip(b, 0, b.size(), 0.004f, 0.06f, 0.8f));
    ev.gain = hurtGain; p->events["player_hurt"] = ev; ++used;
    logf("audio: player hurt: %zu grunt(s)", ev.v.size());
  }
  {   // SND14 (user request): the near-miss whiz from the user's fly-by recording, a clean pass per variant (local-only snd14/flyby_N.wav;
      // bulletwhiz.mp3 without them)
    Impl::Event ev; char fn[40];
    for (int k = 1; k <= 8; ++k) { std::snprintf(fn, sizeof fn, "snd14/flyby_%d.wav", k); if (!decode(fn, b)) break; float pk = peakOf(b); for (float& x : b) x *= 0.8f / pk; ev.v.push_back(b); }
    if (!ev.v.empty()) { ++used; p->events["bulletwhiz"] = ev; }
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
bool Audio::hasSound(const std::string& name) const { return p && (p->events.count(name) || p->bufs.count(name)); }
int Audio::eventVariant(const std::string& name) const { if (!p) return -1; auto it = p->events.find(name); return it == p->events.end() ? -1 : it->second.last; }

void Audio::play(const std::string& name, vec3 pos, float volume, float pitch, float delay) {
  ++playCount; lastPlayed = name; lastPos = pos;
  if (!p) return;
  // resolve: the event (its far layer beyond 90 m), else the procedural buffer, else the name without its last
  // "_suffix" (explosion_grenade -> explosion), so a missing sample never silences an event
  const Buf* src = nullptr; const Buf* stereo = nullptr; std::string used; int evBus = -1; float evRoll = -1, evEar = 2; bool evDuck = false; float evGap = 0; int evMaxV = 0; float gain = 1, minDist = 1; bool shot = false, step = false, uiEv = false;
  std::string n = name;
  for (int tries = 0; tries < 3 && !src; ++tries) {
    std::string cand = n;
    if (glm::length(pos - p->listener) > 90.0f && p->events.count(n + "_far")) cand = n + "_far";
    auto e = p->events.find(cand);
    if (e != p->events.end()) {
      auto& ev = e->second; int k = 0;
      if (ev.v.size() > 1) { k = (int)(p->pick() % (ev.v.size() - 1)); if (ev.last >= 0 && k >= ev.last) ++k; }   // never the same twice running
      ev.last = k; src = &ev.v[k]; stereo = k < (int)ev.st.size() && !ev.st[k].empty() ? &ev.st[k] : nullptr; used = cand; evBus = ev.bus; evRoll = ev.rolloff; evEar = ev.earM; evDuck = ev.duck; evGap = ev.minGap; evMaxV = ev.maxVoices; gain = ev.gain; minDist = ev.minDist; uiEv = ev.kind == "ui"; shot = ev.kind == "shot"; step = ev.kind == "step";
    } else if (auto b = p->bufs.find(n); b != p->bufs.end()) { src = &b->second; used = n; }
    else { size_t u = n.rfind('_'); if (u == std::string::npos) break; n = n.substr(0, u); }
  }
  if (!src) return;
  if (!uiEv) delay = std::max(delay, glm::length(pos - p->listener) / 343.0f);   // BF 8.1 item 10: every world sound arrives after its travel time (callers that already pass it keep theirs)
  if (evGap > 0) {   // SND9: rapid fire (2500 rpm and up): rounds closer than minGap at the same spot are carried by the previous
                     // body (a buzz / roar, not a click train), and the oldest overlapping body fades out (15 ms) past maxVoices
    double tn = ma_engine_get_time_in_milliseconds(&p->engine) / 1000.0;   // audio time
    double& last = p->rapidAt[used + "@" + std::to_string((int)std::floor(pos.x / 4)) + "," + std::to_string((int)std::floor(pos.z / 4))];
    if (tn - last < evGap) return;
    last = tn;
    int live = 0; Impl::Voice* oldest = nullptr;
    for (auto& c : p->voices) if (c.used && c.event == used && !ma_sound_at_end(&c.snd)) { ++live; if (!oldest || c.start < oldest->start) oldest = &c; }
    if (oldest && live >= evMaxV) { ma_sound_set_fade_in_milliseconds(&oldest->snd, -1, 0, 15); ma_sound_set_stop_time_in_milliseconds(&oldest->snd, ma_engine_get_time_in_milliseconds(&p->engine) + 16); }
  }
  lastEvent = used; ++eventPlays[used];
  { static const bool lg = std::getenv("NMK_AUDIO_LOG") != nullptr; if (lg) logf("audio play %s vol %.2f pitch %.2f at %.1f m", used.c_str(), volume, pitch, glm::length(pos - p->listener)); }   // diagnostics
  double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  if (shot && evGap <= 0)   // automatic fire: the previous body of this gun yields to the new shot (no stacking / clipping). SND7: it is
              // out in 5 ms and the new shot starts after it, so the two never sum into the limiter (that dipped each attack
              // up to -3 dB in an AK burst)
    for (auto& c : p->voices) if (c.used && c.event == used && !ma_sound_at_end(&c.snd) && now - c.start < 0.35) {
      ma_sound_set_fade_in_milliseconds(&c.snd, -1, 0, 5);
      ma_sound_set_stop_time_in_milliseconds(&c.snd, ma_engine_get_time_in_milliseconds(&p->engine) + 6);
      delay = std::max(delay, 0.006f);
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
  // WP6: reload / magazine / clip / bolt / pump foley: the Effects level, never the gunfire bus or the gun reverb, louder (mix.json
  // "reload"), and it falls off quickly (heard close by: someone else's reload is a near sound)
  const bool foley = used.rfind("reload_", 0) == 0 || used.rfind("clip_", 0) == 0 || used.rfind("bolt_cycle_", 0) == 0 || used == "mag_in" || used == "mag_out"
                  || used == "bolt" || used == "belt" || used == "shotgun_pump" || used == "rocket_ready" || used == "rocket_in";
  if (foley) {
    float tr = 1; size_t us = used.find('_');
    if (us != std::string::npos) { std::string id = used.substr(used.rfind("bolt_cycle_", 0) == 0 ? 11 : us + 1); auto it = p->reloadTrim.find(id); if (it != p->reloadTrim.end()) tr = it->second; }
    volume *= p->reloadGain * tr; evBus = 0; if (evRoll <= 0) evRoll = 2.0f;
  }
  { const bool gunBus = shot || used.rfind("shot_", 0) == 0 || used.rfind("tail_", 0) == 0 || used.rfind("explosion", 0) == 0 || used.find("_fire") != std::string::npos || used.find("cannon") != std::string::npos;
    if (gunBus) volume *= gunfireGain; }   // UI1b: the gunfire bus
  // SND5: a gun event with its stereo recording (the custom gunshots) at the listener (within 2 m: the player's own shot)
  // plays it as recorded, not spatialised (the spatialiser cost a mono shot about 6 dB here); other sounds as before
  const bool atEar = stereo && glm::length(pos - p->listener) < evEar; if (!atEar) stereo = nullptr;
  // SND5: gunfire (shots of every class, tank / field guns, recorded tails) carries further (full level within 3 m, the
  // M249 / MG 42 5 m), goes through the gun reverb and ducks the ambience beds
  const bool gun = shot || used.rfind("shot_", 0) == 0 || used.rfind("tank_", 0) == 0 || used.rfind("tail_", 0) == 0 || used == "pak_fire" || used == "flak_fire";
  const bool fifty = used == "shot_modern_sniper_50";
  const bool heavy = used == "shot_mg42" || used == "shot_modern_lmg" || used == "shot_minigun" || used == "shot_cannon30" || used == "tail_mg42" || used == "tail_modern_lmg" || used.rfind("shot_m2hb", 0) == 0 || used.rfind("tail_m2hb", 0) == 0;
  if (gun) minDist = std::max(minDist, fifty ? 8.0f : heavy ? 5.0f : 3.0f);
  if ((gun || evDuck) && glm::length(pos - p->listener) < (evDuck ? 150.0f : 60.0f)) p->duckT = now;   // SND6: the .50 full level to 8 m
  ma_audio_buffer_config c = stereo ? ma_audio_buffer_config_init(ma_format_f32, 2, stereo->size() / 2, stereo->data(), nullptr)
                                    : ma_audio_buffer_config_init(ma_format_f32, 1, src->size(), src->data(), nullptr);
  c.sampleRate = SR;
  ma_audio_buffer_init(&c, &v->buf);
  ma_sound_init_from_data_source(&p->engine, &v->buf, atEar ? MA_SOUND_FLAG_NO_SPATIALIZATION : 0, nullptr, &v->snd);
  v->bus = evBus >= 0 ? evBus : gun ? (fifty ? 3 : heavy ? 2 : 1) : 0; p->route(v->snd, v->bus);
  ma_sound_set_position(&v->snd, pos.x, pos.y, pos.z);
  ma_sound_set_min_distance(&v->snd, minDist);
  ma_sound_set_rolloff(&v->snd, evRoll > 0 ? evRoll : fifty ? 0.8f : 1.2f);   // SND6: the .50 carries further; SND9: an event's own
  ma_sound_set_volume(&v->snd, volume);
  if (pitch != 1.0f) ma_sound_set_pitch(&v->snd, std::max(0.05f, pitch));
  if (delay > 0) ma_sound_set_start_time_in_milliseconds(&v->snd, ma_engine_get_time_in_milliseconds(&p->engine) + (ma_uint64)(delay * 1000));
  ma_sound_start(&v->snd);
  v->used = true; v->event = used; v->start = now + delay;
}

void Audio::casing(vec3 feet, int shooter, float delay, bool soft) {
  if (!p || !p->events.count("casing")) return;
  double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  double& last = p->casingAt[shooter];
  int live = 0; for (auto& c : p->voices) if (c.used && c.event == "casing" && (c.start > now || !ma_sound_at_end(&c.snd))) ++live;
  if (glm::length(feet - p->listener) > 15.0f || now - last < 0.08 || live >= 6) { ++casingsSkipped; return; }
  last = now; ++casingsPlayed;
  std::uniform_real_distribution<float> u(0, 1);
  play("casing", feet + vec3(0, 0.05f, 0), (soft ? 0.12f : 0.3f) * p->casingGain * (0.8f + 0.2f * u(p->pick)), 0.9f + 0.2f * u(p->pick), delay);
}

void Audio::setListener(vec3 pos, vec3 fwd) {
  listener_ = pos;
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
// a looping file decoded whole (ch channels) so the ends can be cross-faded: a streamed mp3 loop clicks and gaps at the
// seam. The encoder silence is trimmed at both ends, then the last 3 s (at most a quarter) blended into the first.
static bool loopBuf(const std::string& f, int ch, Buf& b) {
  ma_decoder_config dc = ma_decoder_config_init(ma_format_f32, (ma_uint32)ch, SR);
  ma_uint64 frames = 0; void* data = nullptr;
  if (f.empty() || ma_decode_file(f.c_str(), &dc, &frames, &data) != MA_SUCCESS || !data) return false;
  b.assign((float*)data, (float*)data + frames * ch); ma_free(data, nullptr);
  size_t n = (size_t)frames, a = 0, e = n;
  auto loud = [&](size_t k) { float s = 0; for (int c = 0; c < ch; ++c) s += std::fabs(b[ch * k + c]); return s > 0.002f * ch; };
  while (a < e && !loud(a)) ++a;
  while (e > a && !loud(e - 1)) --e;
  size_t xf = std::min((size_t)(3 * SR), (e - a) / 4);
  if (e - a <= (size_t)SR || xf == 0) return false;
  for (size_t k = 0; k < xf; ++k) { float w = k / (float)xf, g0 = std::sqrt(w), g1 = std::sqrt(1 - w);   // equal power
    for (int c = 0; c < ch; ++c) b[ch * (a + k) + c] = b[ch * (a + k) + c] * g0 + b[ch * (e - xf + k) + c] * g1; }
  b.erase(b.begin() + ch * (e - xf), b.end()); b.erase(b.begin(), b.begin() + ch * a);
  return true;
}
void Audio::setBed(int slot, const fs::path& file, float volume) {
  if (!p || slot < 0 || slot > 1) return;
  std::string f = file.string();
  if (f != p->bedFile[slot]) {
    if (p->bedOn[slot]) { ma_sound_uninit(&p->bed[slot]); ma_audio_buffer_uninit(&p->bedAB[slot]); p->bedOn[slot] = false; }
    p->bedFile[slot] = f;
    Buf& b = p->bedBuf[slot];
    if (loopBuf(f, 2, b)) {
      ma_audio_buffer_config c = ma_audio_buffer_config_init(ma_format_f32, 2, b.size() / 2, b.data(), nullptr);
      c.sampleRate = SR;
      if (ma_audio_buffer_init(&c, &p->bedAB[slot]) == MA_SUCCESS) {
        if (ma_sound_init_from_data_source(&p->engine, &p->bedAB[slot], MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &p->bed[slot]) == MA_SUCCESS) {
          p->bedOn[slot] = true; p->route(p->bed[slot]); ma_sound_set_looping(&p->bed[slot], MA_TRUE); ma_sound_start(&p->bed[slot]);
        } else ma_audio_buffer_uninit(&p->bedAB[slot]);
      }
    }
  }
  p->bedVol[slot] = volume;
  {   // SND5: -3 dB under nearby gunfire, back over 1.5 s
    double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    float duck = 1.0f - 0.3f * (float)std::clamp(1.0 - (now - p->duckT) / 1.5, 0.0, 1.0);
    if (p->bedOn[slot]) ma_sound_set_volume(&p->bed[slot], volume * ambienceGain * duck);
  }
}
void Audio::ambVoice(const std::string& id, const fs::path& file, bool spatial, vec3 pos, float volume, float minD, float maxD) {
  if (!p) return;
  auto& slot = p->amb[id];
  if (!slot) {   // first use: decode (point sources mono), start looping from a random place (two voices of one file never in phase)
    slot = std::make_unique<Impl::Amb>(); Impl::Amb& a = *slot; const int ch = spatial ? 1 : 2;
    if (!loopBuf(file.string(), ch, a.b)) { logf("audio: map ambience %s: cannot loop %s", id.c_str(), file.string().c_str()); return; }
    ma_audio_buffer_config c = ma_audio_buffer_config_init(ma_format_f32, (ma_uint32)ch, a.b.size() / ch, a.b.data(), nullptr);
    c.sampleRate = SR;
    if (ma_audio_buffer_init(&c, &a.ab) != MA_SUCCESS) return;
    if (ma_sound_init_from_data_source(&p->engine, &a.ab, spatial ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &a.snd) != MA_SUCCESS) { ma_audio_buffer_uninit(&a.ab); return; }
    a.on = true; p->route(a.snd); ma_sound_set_looping(&a.snd, MA_TRUE);
    if (spatial) { ma_sound_set_attenuation_model(&a.snd, ma_attenuation_model_linear); ma_sound_set_rolloff(&a.snd, 1.0f); }
    ma_sound_seek_to_pcm_frame(&a.snd, (ma_uint64)(p->pick() % (a.b.size() / ch)));
    ma_sound_set_volume(&a.snd, 0); ma_sound_start(&a.snd);
  }
  Impl::Amb& a = *slot; if (!a.on) return;
  if (spatial) { ma_sound_set_position(&a.snd, pos.x, pos.y, pos.z); ma_sound_set_min_distance(&a.snd, minD); ma_sound_set_max_distance(&a.snd, std::max(maxD, minD + 0.1f)); }
  double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  float duck = 1.0f - 0.3f * (float)std::clamp(1.0 - (now - p->duckT) / 1.5, 0.0, 1.0);   // under nearby gunfire, as the beds
  ma_sound_set_volume(&a.snd, std::max(0.0f, volume) * ambienceGain * duck);
}
std::vector<std::string> Audio::ambIds() const { std::vector<std::string> v; if (p) for (auto& [id, a] : p->amb) if (a->on) v.push_back(id); return v; }
void Audio::ambClear() {
  if (!p) return;
  for (auto& [id, a] : p->amb) if (a->on) { ma_sound_uninit(&a->snd); ma_audio_buffer_uninit(&a->ab); }
  p->amb.clear();
}
void Audio::setMenuMusic(bool on) {
  if (!p || !p->musicOn || on == p->musicPlaying) return;
  p->musicPlaying = on;
  if (on) { ma_sound_set_stop_time_in_pcm_frames(&p->music, ~(ma_uint64)0); ma_sound_set_fade_in_milliseconds(&p->music, 0, 1, 1000); ma_sound_start(&p->music); logf("audio: menu music starts"); }   // resumes where it stopped
  else { ma_sound_stop_with_fade_in_milliseconds(&p->music, 1000); logf("audio: menu music fades out"); }
}
void Audio::setHeartbeat(float level, float rate) {
  if (!p || !p->hbOn) return;
  const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  const float dt = p->hbT < 0 ? 0.0f : (float)std::min(0.25, now - p->hbT); p->hbT = now;
  level = std::clamp(level, 0.0f, 1.5f);
  heartbeatNow = level > heartbeatNow ? std::min(level, heartbeatNow + dt / 0.4f) : std::max(level, heartbeatNow - dt / 0.8f);   // in 0.4 s, out 0.8 s
  if (heartbeatNow > 0.001f && !p->hbPlaying) { ma_sound_seek_to_pcm_frame(&p->hb, 0); ma_sound_start(&p->hb); p->hbPlaying = true; }
  if (heartbeatNow <= 0.001f && p->hbPlaying) { ma_sound_stop(&p->hb); p->hbPlaying = false; }
  ma_sound_set_volume(&p->hb, heartbeatNow * heartbeatGain * effectsGain);
  ma_sound_set_pitch(&p->hb, std::clamp(rate, 0.8f, 1.3f));
}
void Audio::setUnderwater(float k) {
  k = std::clamp(k, 0.0f, 1.0f);
  underwater = k;
  if (!p || !p->lpfOk) return;
  if ((k > 0) != p->muffled) { p->muffled = k > 0; p->rerouteAll(); }   // SND5: the muffle is in the chain only under water
  float cut = k > 0 ? 20000.0f * std::pow(550.0f / 20000.0f, k) : 20000.0f;   // 20 kHz -> 550 Hz (log blend)
  if (std::fabs(cut - p->lpfCut) < 1.0f) return;
  p->lpfCut = cut;
  ma_lpf_config c = ma_lpf_config_init(ma_format_f32, ma_engine_get_channels(&p->engine), ma_engine_get_sample_rate(&p->engine), cut, 2);
  ma_lpf_node_reinit(&c, &p->lpf);
  ma_node_set_output_bus_volume(&p->lpf, 0, 1.0f - 0.35f * k);
}

void Audio::setRoom(float r) { if (p && p->verbOk) for (auto& v : p->verb) v.setRoom(std::clamp(r, 0.0f, 1.0f)); }   // audio thread reads it too: a one-frame mix of old / new lengths is harmless

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

// ---- SND5 diagnostics: the player's own shot as heard, against the mp3 itself
namespace {
struct Measure { float peak = 0, rms = 0, mid = 0, high = 0, tail = -200; };   // dB: peak / RMS (full scale), band energy 1-4 kHz and 4-10 kHz, RMS 0.4-1.4 s after the onset
size_t onsetOf(const std::vector<float>& x, size_t from, float th) { size_t i = from; while (i < x.size() && std::fabs(x[i]) < th) ++i; return i; }
Measure measure(const std::vector<float>& x, float fs, size_t from = 0) {
  Measure m; float pk = 1e-9f; for (size_t i = from; i < x.size() && i < from + (size_t)(1.0f * fs); ++i) pk = std::max(pk, std::fabs(x[i]));
  size_t on = onsetOf(x, from, 0.3f * pk); on = on > from + (size_t)(0.005f * fs) ? on - (size_t)(0.005f * fs) : from;
  size_t n = std::min(x.size() - on, (size_t)(0.5f * fs)); double q = 0; for (size_t i = 0; i < n; ++i) q += (double)x[on + i] * x[on + i];
  auto band = [&](float f0, float f1) {   // Goertzel at 125 Hz steps, Hann window: mean power
    double sum = 0; int k = 0;
    for (float f = f0; f <= f1; f += 125.0f, ++k) { double w = 2 * 3.14159265358979 * f / fs, c = 2 * std::cos(w), s1 = 0, s2 = 0;
      for (size_t i = 0; i < n; ++i) { double hw = 0.5 - 0.5 * std::cos(2 * 3.14159265358979 * i / n); double s0 = x[on + i] * hw + c * s1 - s2; s2 = s1; s1 = s0; }
      sum += (s1 * s1 + s2 * s2 - c * s1 * s2) / ((double)n * n); }
    return (float)(10 * std::log10(sum / std::max(1, k) + 1e-20)); };
  m.peak = 20 * std::log10(pk); m.rms = (float)(10 * std::log10(q / std::max<size_t>(1, n) + 1e-20)); m.mid = band(1000, 4000); m.high = band(4000, 10000);
  size_t t0 = on + (size_t)(0.4f * fs), t1 = std::min(x.size(), on + (size_t)(1.4f * fs)); if (t1 > t0) { double r = 0; for (size_t i = t0; i < t1; ++i) r += (double)x[i] * x[i]; m.tail = (float)(10 * std::log10(r / (t1 - t0) + 1e-20)); }
  return m;
}
}
void Audio::fidelityReport(const fs::path& dir) {
  if (!p) { logf("fidelity: no audio engine"); return; }
  struct Pair { const char* ev; const char* file; int take; float dist; } pairs[] = {   // take: which shot of the source (onsets 1 s apart); dist: m ahead (0.6: the player's muzzle)
    {"shot_kar98k", "guns/sniper_m24.mp3", 0, 0.6f}, {"shot_kar98k_zf39", "guns/sniper_m24.mp3", 0, 0.6f}, {"shot_modern_sniper_3", "guns/sniper_m24.mp3", 0, 0.6f}, {"shot_m1_garand", "guns/rifle_a.mp3", 0, 0.6f}, {"shot_modern_ak", "guns/smg_a.mp3", 0, 0.6f},
    {"shot_modern_ak12", "guns/smg_a.mp3", 0, 0.6f}, {"shot_stg44", "guns/smg_a.mp3", 0, 0.6f}, {"shot_modern_lmg", "snd16/m249_shot_1.wav", 0, 0.6f},
    {"shot_mg42", "snd16/mg_shot_1.wav", 0, 0.6f},
    {"shot_modern_m16", "guns/rifle_c.mp3", 0, 0.6f}, {"shot_modern_m4_irons", "guns/rifle_c.mp3", 2, 0.6f}, {"shot_modern_fal", "guns/rifle_c.mp3", 3, 0.6f},
    {"shot_modern_sniper_bolt", "guns/sniper_m24.mp3", 0, 0.6f}, {"shot_modern_sniper_50", "guns/sniper_heavy.mp3", 0, 0.6f}, {"shot_modern_sniper_50", "guns/sniper_heavy.mp3", 0, 20.0f}, {"shot_m2hb_carry", "guns/hmg_burst.mp3", 0, 0.6f}, {"shot_m2hb", "guns/hmg_burst.mp3", 0, 0.6f}, {"shot_mp40", "guns/smg_a.mp3", 0, 0.6f}, {"shot_luger", "guns/pistol_a.mp3", 0, 0.6f},
    {"shot_modern_shotgun", "guns/shotgun.mp3", 0, 0.6f}, {"shot_kar98k", "guns/sniper_m24.mp3", 0, 20.0f}, {"step", "footsteps.mp3", 0, 1.6f},
    {"tank_88_before", "guns/tank_shot.mp3", 0, 4.0f}, {"tank_88", "guns/tank_shot.mp3", 0, 4.0f}, {"tank_88_before", "guns/tank_shot.mp3", 0, 100.0f}, {"tank_88", "guns/tank_shot.mp3", 0, 100.0f},
    {"explosion_big", "guns/shell_impact.mp3", 0, 30.0f}, {"shell_hit", "guns/launcher_fire.mp3", 0, 30.0f}, {"explosion_big", "guns/shell_impact.mp3", 0, 200.0f}, {"shell_hit", "guns/launcher_fire.mp3", 0, 200.0f}};
  {   // SND9: the tank gun as it was before SND9 (miniaudio's linear decode to mono, 0.9 peak, full level to 40 m, roll-off 1.2), for comparison
    ma_decoder_config dc = ma_decoder_config_init(ma_format_f32, 1, SR); ma_uint64 fr = 0; void* data = nullptr;
    if (ma_decode_file((dir / "guns/tank_shot.mp3").string().c_str(), &dc, &fr, &data) == MA_SUCCESS && data) {
      Buf b((float*)data, (float*)data + fr); ma_free(data, nullptr);
      float pk = 1e-4f; for (float x : b) pk = std::max(pk, std::fabs(x)); size_t a = 0; while (a < b.size() && std::fabs(b[a]) < 0.2f * pk) ++a; a = a > (size_t)(SR / 100) ? a - SR / 100 : 0;
      Buf s2(b.begin() + a, b.begin() + std::min(b.size(), a + (size_t)(5.0f * SR))); size_t f = std::min(s2.size(), (size_t)(0.3f * SR));
      for (size_t j = 0; j < s2.size(); ++j) s2[j] *= 0.9f / pk * (j + f >= s2.size() ? (float)(s2.size() - j) / f : 1.0f);
      Impl::Event ev; ev.minDist = 40.0f; ev.v.push_back(std::move(s2)); p->events["tank_88_before"] = ev;
    }
  }
  setListener(vec3(0), vec3(0, 0, -1));
  auto render = [&](const char* ev, vec3 pos, float secs) {
    for (auto& c : p->voices) if (c.used) ma_sound_stop(&c.snd);
    std::vector<float> st(SR * 2 * 4), mid((size_t)(SR * secs)); for (int k = 0; k < 20; ++k) ma_engine_read_pcm_frames(&p->engine, st.data(), SR / 10, nullptr);   // flush 2 s (tails)
    if (ev) play(ev, pos, 1.0f);
    for (size_t done = 0; done < mid.size();) { ma_uint64 n = std::min<size_t>(SR, mid.size() - done); ma_engine_read_pcm_frames(&p->engine, st.data(), n, nullptr); for (ma_uint64 i = 0; i < n; ++i) mid[done + i] = 0.5f * (st[2 * i] + st[2 * i + 1]); done += n; }
    return mid;
  };
  if (const char* fm = std::getenv("NMK_AUDIO_FIDELITY"); fm && std::string(fm) == "guns") {   // SND11: every gun's shot as played, at 3 m and 30 m
    std::vector<std::string> ids; for (auto& [k, e] : p->events) if (k.rfind("shot_", 0) == 0 && k.find("_far") == std::string::npos) ids.push_back(k);
    std::sort(ids.begin(), ids.end());
    for (auto& id : ids) for (float dist : {3.0f, 30.0f}) {
      std::vector<float> m = render(id.c_str(), vec3(0.15f, -0.1f, -dist), 2.0f);
      Measure b = measure(m, (float)SR);
      logf("gunlevel %-26s %4.0f m  peak %6.1f  rms %6.1f  1-4k %6.1f  4-10k %6.1f  tail %6.1f dB", id.c_str(), dist, b.peak, b.rms, b.mid, b.high, b.tail);
    }
    return;
  }
  for (auto& pr : pairs) {
    if (!hasEvent(pr.ev)) { logf("fidelity %s: no event", pr.ev); continue; }
    ma_decoder_config dc = ma_decoder_config_init(ma_format_f32, 0, 0); ma_uint64 fr = 0; void* data = nullptr;
    if (ma_decode_file((dir / pr.file).string().c_str(), &dc, &fr, &data) != MA_SUCCESS || !data) { logf("fidelity %s: cannot decode %s", pr.ev, pr.file); continue; }
    std::vector<float> src(fr); const float* d = (const float*)data;
    for (ma_uint64 i = 0; i < fr; ++i) { float s = 0; for (ma_uint32 c = 0; c < dc.channels; ++c) s += d[i * dc.channels + c]; src[i] = s / dc.channels; }   // mid (L+R)/2
    ma_free(data, nullptr);
    size_t from = 0; { float pk = 1e-9f; for (float v : src) pk = std::max(pk, std::fabs(v)); size_t last = 0; int k = -1;   // the take's onset
      for (size_t i = 0; i < src.size(); ++i) if (std::fabs(src[i]) > 0.5f * pk && (k < 0 || i - last >= dc.sampleRate)) { if (++k == pr.take) { from = i > dc.sampleRate / 20 ? i - dc.sampleRate / 20 : 0; break; } last = i; } else if (std::fabs(src[i]) > 0.5f * pk) last = i; }
    std::vector<float> mid = render(pr.ev, vec3(0.15f, -0.1f, -pr.dist), 2.0f);
    Measure a = measure(src, (float)dc.sampleRate, from), b = measure(mid, (float)SR);
    logf("fidelity %-22s %-19s take %d at %4.1f m (%5u Hz %u ch): peak %6.1f -> %6.1f (%+5.1f)  rms %6.1f -> %6.1f (%+5.1f)  1-4k %+5.1f  4-10k %+5.1f  tilt %+5.1f dB  tail %6.1f -> %6.1f",
         pr.ev, pr.file, pr.take, pr.dist, dc.sampleRate, dc.channels, a.peak, b.peak, b.peak - a.peak, a.rms, b.rms, b.rms - a.rms, b.mid - a.mid, b.high - a.high, (b.high - b.mid) - (a.high - a.mid), a.tail, b.tail);
  }
  {   // the ambience bed (battlefield, as Explore plays it: 0.7) over 6 s, and with a nearby shot
    fs::path amb = dir.parent_path() / "ambience" / "battlefield.mp3";
    setBed(1, amb, 0.7f); std::vector<float> m = render(nullptr, vec3(0), 6.0f);
    double q = 0; float pk = 0; for (float v : m) { q += (double)v * v; pk = std::max(pk, std::fabs(v)); }
    logf("fidelity ambience bed battlefield.mp3 x0.7: peak %6.1f  rms %6.1f dB", 20 * std::log10(pk + 1e-9f), 10 * std::log10(q / m.size() + 1e-20));
    setBed(1, {}, 0);
  }
  for (const char* ev : {"shot_modern_ak", "shot_modern_m4_irons", "shot_modern_m16", "shot_modern_fal", "shot_modern_lmg", "shot_mg42"}) {   // SND7: sustained fire, 13 rounds 80 ms apart (750 rpm), as the player
    if (!hasEvent(ev)) continue;
    for (auto& c : p->voices) if (c.used) ma_sound_stop(&c.snd);
    std::vector<float> st(SR * 2); for (int k = 0; k < 20; ++k) ma_engine_read_pcm_frames(&p->engine, st.data(), SR / 10, nullptr);
    const int step = SR * 8 / 100; std::vector<float> mid; float gMin = 1;
    for (int k = 0; k < 13; ++k) { play(ev, vec3(0.15f, -0.1f, -0.6f), 1.0f); ma_engine_read_pcm_frames(&p->engine, st.data(), step, nullptr); for (int i = 0; i < step; ++i) mid.push_back(0.5f * (st[2 * i] + st[2 * i + 1])); gMin = std::min(gMin, p->lim.g); }
    double q = 0; float pk = 0; for (float v : mid) { q += (double)v * v; pk = std::max(pk, std::fabs(v)); }
    logf("fidelity burst %-22s 13 rounds at 750 rpm (1.04 s): peak %6.1f  rms %6.1f dB  limiter gain low %.2f", ev, 20 * std::log10(pk + 1e-9f), 10 * std::log10(q / mid.size() + 1e-20), gMin);
  }
  for (auto [ev, rpm] : {std::pair<const char*, int>{"shot_minigun", 2500}, {"shot_cannon30", 1200}, {"shot_mg42", 1200}}) {   // SND9: the vehicle rapid-fire guns at their rates, 3 m off, 1.5 s, then the run-out
    if (!hasEvent(ev)) continue;
    for (auto& c : p->voices) if (c.used) ma_sound_stop(&c.snd);
    std::vector<float> st(SR * 2); for (int k = 0; k < 20; ++k) ma_engine_read_pcm_frames(&p->engine, st.data(), SR / 10, nullptr);
    const int step = SR * 60 / rpm, n = SR * 3 / 2 / step; std::vector<float> mid; float gMin = 1; int played0 = eventPlays[ev];
    for (int k = 0; k < n; ++k) { play(ev, vec3(1.0f, 0.5f, -2.8f), 1.0f); ma_engine_read_pcm_frames(&p->engine, st.data(), step, nullptr); for (int i = 0; i < step; ++i) mid.push_back(0.5f * (st[2 * i] + st[2 * i + 1])); gMin = std::min(gMin, p->lim.g); }
    double q = 0; float pk = 0; for (float v : mid) { q += (double)v * v; pk = std::max(pk, std::fabs(v)); }
    std::vector<float> e10; for (size_t i = 0; i + SR / 100 <= mid.size(); i += SR / 100) { double r = 0; for (int j = 0; j < SR / 100; ++j) r += (double)mid[i + j] * mid[i + j]; e10.push_back((float)(10 * std::log10(r / (SR / 100) + 1e-20))); }
    float lo = 0, hi = -200; for (size_t i = e10.size() / 5; i < e10.size(); ++i) { lo = std::min(lo, e10[i]); hi = std::max(hi, e10[i]); }   // 10 ms loudness over the sustained part: a small swing = a roar, a big one = a click train
    logf("fidelity rapid %-14s %d rpm, %d rounds over 1.5 s (%d sounded) at 3 m: peak %6.1f  rms %6.1f dB  10 ms loudness swing %.1f dB  limiter gain low %.2f", ev, rpm, n, eventPlays[ev] - played0, 20 * std::log10(pk + 1e-9f), 10 * std::log10(q / mid.size() + 1e-20), hi - lo, gMin);
  }
  if (hasEvent("casing")) for (float gn : {1.0f, p->casingGain}) {   // SND6: a case landing at the player's feet (hard ground), at the SND4 level and now
    for (auto& c : p->voices) if (c.used) ma_sound_stop(&c.snd);
    std::vector<float> st(SR * 2); for (int k = 0; k < 20; ++k) ma_engine_read_pcm_frames(&p->engine, st.data(), SR / 10, nullptr);
    float keep = p->casingGain; p->casingGain = gn; p->casingAt.clear(); casing(vec3(0.2f, -1.6f, -0.1f), 7000, 0, false); p->casingGain = keep;
    ma_engine_read_pcm_frames(&p->engine, st.data(), SR, nullptr);
    double q = 0; float pk = 0; for (int i = 0; i < SR; ++i) { float v = 0.5f * (st[2 * i] + st[2 * i + 1]); q += (double)v * v; pk = std::max(pk, std::fabs(v)); }
    logf("fidelity casing x%.1f (hard ground; soft is 0.4 of it): peak %6.1f  rms over 1 s %6.1f dB", gn, 20 * std::log10(pk + 1e-9f), 10 * std::log10(q / SR + 1e-20));
  }
}
