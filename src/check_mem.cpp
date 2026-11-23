// MEM1 --mem-probe: committed (private) and resident memory and CPU at the main menu, a small map, a large map (loaded
// twice: a per-load leak shows as growth), and a 20-bot match on it. [mem] lines; NMK_MEMLOG=1 also stamps every log line.
#include "selftest.h"
#include <chrono>
#include <map>
#include "mainmenu.h"
#ifdef _WIN32
#include <windows.h>
#endif

#include "game.h"
#include "mpui.h"
#include "ai_core.h"
#include "scenario.h"
#include "lines.h"

namespace {
bool startMatch(App& a, int attackers, std::string& err) {   // as selftest_ai_harness.cpp's
  if (!a.doc.extra.is_object() || !a.doc.extra.contains("scenario")) { err = "map has no scenario"; return false; }
  Scenario sc = Scenario::fromJson(a.doc.extra["scenario"]); sc.rules.attackerAI = attackers;
  gUi.matchScenario = sc.toJson(); gUi.loaded = false;
  if (!gGame) gGame = new Game(a);
  gGame->pendingStart = true;
  bool ok = a.enterExplore() && gGame->active && aiCore();
  a.useScripted = true; a.scriptedInput = ExploreInput{};
  if (!ok) err = "match did not start: " + gGame->setupError;
  return ok;
}
int phase = 0; float t = 0; int frames = 0; double cpu0 = 0, wall0 = 0;
double cpuSec() {
#ifdef _WIN32
  FILETIME c, e, k, u; GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
  auto s = [](FILETIME f) { return (((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime) * 1e-7; }; return s(k) + s(u);
#else
  return 0;
#endif
}
double wall() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
void cpuCopies(App& a) {   // MEM1: CPU-side mesh copies the built assets and placed lines keep
  auto mb = [](const MeshData* m) { return m ? (m->v.capacity() * sizeof(Vertex) + m->i.capacity() * 4) / 1048576.0 : 0.0; };
  double hl = 0, lf = 0, ln = 0; int built = 0;
  for (auto& ap : a.lib.assets) { if (!ap->built) continue; ++built; hl += mb(ap->hlodOpaque.get()) + mb(ap->hlodGlass.get());
    for (auto& it : ap->interactables) for (auto& l : it.leaves) lf += mb(l.cpuO.get()) + mb(l.cpuG.get()); }
  for (auto& [id, r] : a.rt) if (r.line) ln += mb(&r.line->mesh);
  logf("[mem]   CPU mesh copies: HLOD %.0f MB, door / window leaves %.0f MB, line meshes %.0f MB (%d assets built)", hl, lf, ln, built);
  logf("[mem]   furniture batches: %zu MB of vertices on the GPU (%d pieces batched)", a.interior.batchVerts * sizeof(GpuVertex) >> 20, a.interior.batchedPieces);
}
// MEM3: where the commit is: every process heap walked (bytes in use, free inside its committed regions = fragmentation,
// big blocks the heap took straight from VirtualAlloc), the address space by type, and the GPU's own count (NVX)
void breakdown() {
#ifdef _WIN32
  double busy = 0, freeIn = 0, regions = 0, big = 0; long blocks = 0;
  HANDLE hs[256]; DWORD nh = GetProcessHeaps(256, hs);
  for (DWORD h = 0; h < nh && h < 256; ++h) {
    if (!HeapLock(hs[h])) continue;
    PROCESS_HEAP_ENTRY e{}; e.lpData = nullptr;
    while (HeapWalk(hs[h], &e)) {
      if (e.wFlags & PROCESS_HEAP_REGION) regions += e.Region.dwCommittedSize;
      else if (e.wFlags & PROCESS_HEAP_ENTRY_BUSY) { busy += e.cbData; ++blocks; if (e.cbData >= (512u << 10)) big += e.cbData; }
      else if (!(e.wFlags & PROCESS_HEAP_UNCOMMITTED_RANGE)) freeIn += e.cbData;
    }
    HeapUnlock(hs[h]);
  }
  // committed private memory by allocation (VirtualAlloc reservation): heap regions are subtracted below; the rest by
  // allocation size, and write-combined pages (driver staging / mapped buffers) apart
  double priv = 0, mapped = 0, image = 0, wc = 0, bySize[4] = {0, 0, 0, 0}; MEMORY_BASIC_INFORMATION mi; char* p = nullptr;
  std::map<void*, double> perAlloc;
  while (VirtualQuery(p, &mi, sizeof mi) == sizeof mi) {
    if (mi.State == MEM_COMMIT) (mi.Type == MEM_PRIVATE ? priv : mi.Type == MEM_MAPPED ? mapped : image) += (double)mi.RegionSize;
    if (mi.State == MEM_COMMIT && mi.Type == MEM_PRIVATE) { perAlloc[mi.AllocationBase] += (double)mi.RegionSize; if (mi.Protect & PAGE_WRITECOMBINE) wc += (double)mi.RegionSize; }
    p = (char*)mi.BaseAddress + mi.RegionSize; if ((uintptr_t)p < (uintptr_t)mi.BaseAddress) break;
  }
  for (auto& [base, sz] : perAlloc) bySize[sz < (1 << 20) ? 0 : sz < (16 << 20) ? 1 : sz < (128 << 20) ? 2 : 3] += sz;
  GLint tot = 0, avail = 0; glGetIntegerv(0x9048, &tot); glGetIntegerv(0x9049, &avail); glGetError();   // GL_NVX_gpu_memory_info (KB); 0 elsewhere
  const double M = 1048576.0;
  logf("[mem]   heaps (%lu): %.0f MB in use in %ld blocks (%.0f MB of them >= 512 KB), %.0f MB free inside %.0f MB of committed regions; committed private %.0f MB (outside the heaps' regions %.0f MB), mapped %.0f MB, images %.0f MB; GPU memory in use %.0f MB",
       (unsigned long)nh, busy / M, blocks, big / M, freeIn / M, regions / M, priv / M, (priv - regions) / M, mapped / M, image / M, (tot - avail) / 1024.0);
  logf("[mem]   private by allocation: under 1 MB %.0f MB, 1-16 MB %.0f MB, 16-128 MB %.0f MB, 128 MB and up %.0f MB (%zu allocations); write-combined %.0f MB", bySize[0] / M, bySize[1] / M, bySize[2] / M, bySize[3] / M, perAlloc.size(), wc / M);
#endif
}
void report(const char* stage) {
  ProcMem m = processMemory(); double c = cpuSec(), w = wall();
  logf("[mem] %-26s commit %6.0f MB  working set %6.0f MB  CPU %5.1f %% of one core over %.1f s (%d frames)", stage, m.PrivateBytes / 1048576.0, m.WorkingSetSize / 1048576.0,
       w > wall0 ? 100.0 * (c - cpu0) / (w - wall0) : 0.0, w - wall0, frames);
  logf("[mem]   %s", vramLine().c_str());   // V0
  logf("%s", vramInventory(20).c_str());   // V0b
  cpu0 = c; wall0 = w; frames = 0;
}
}

// V0: the driver's view (GL_NVX_gpu_memory_info, KB; 0 on other vendors) and the characters' own bytes (gCharBytes)
// shortcut: NVIDIA only (AMD's GL_ATI_meminfo is unreliable on consumer cards); DXGI QueryVideoMemoryInfo when another vendor matters
std::string vramLine() {
  GLint ded = 0, tot = 0, avail = 0, evN = 0, evKB = 0;
  glGetIntegerv(0x9047, &ded); glGetIntegerv(0x9048, &tot); glGetIntegerv(0x9049, &avail); glGetIntegerv(0x904A, &evN); glGetIntegerv(0x904B, &evKB);
  while (glGetError() != GL_NO_ERROR) {}
  const double M = 1048576.0; char b[512];
  (void)evKB;   // the driver's evicted total is cumulative since boot and overflows its 32-bit KB field: only the count is printed (its change between lines is the signal)
  std::snprintf(b, sizeof b, "[vram] GPU %s in use %.0f of %.0f MB, evictions %u; characters: %d instances %.1f MB, %d types %.1f MB geometry + %.1f MB textures",
                ded ? "(NVX)" : "(no NVX)", (tot - avail) / 1024.0, ded / 1024.0, (unsigned)evN, gCharBytes.nInst, gCharBytes.inst / M, gCharBytes.nType, gCharBytes.type / M, gCharBytes.tex / M);
  return b;
}
// V0b: every live GL texture, buffer and renderbuffer (ids scanned up to NMK_VRAM_SCAN, default 300000), their bytes by
// kind and the largest few named by size and format: which holders the next VRAM stage should go after
static const char* fmtName(GLint f) {
  switch (f) {
    case GL_RGBA8: return "RGBA8"; case GL_SRGB8_ALPHA8: return "SRGB8_A8"; case GL_RGBA16F: return "RGBA16F"; case GL_RGBA32F: return "RGBA32F";
    case GL_R11F_G11F_B10F: return "R11G11B10F"; case GL_R16F: return "R16F"; case GL_RG16F: return "RG16F"; case GL_R8: return "R8"; case GL_RG8: return "RG8";
    case GL_DEPTH_COMPONENT32F: return "D32F"; case GL_DEPTH_COMPONENT24: return "D24"; case GL_DEPTH_COMPONENT16: return "D16"; case GL_DEPTH24_STENCIL8: return "D24S8"; case GL_DEPTH32F_STENCIL8: return "D32FS8";
    case 0x83F1: return "BC1"; case 0x8C4D: return "BC1s"; case 0x83F3: return "BC3"; case 0x8C4F: return "BC3s"; case 0x8DBD: return "BC5"; case 0x8DBB: return "BC4"; case 0x8E8C: return "BC7";
    case GL_RGB10_A2: return "RGB10A2"; case GL_R32F: return "R32F"; case GL_RG32F: return "RG32F"; case GL_R16: return "R16"; case GL_RGBA16: return "RGBA16";
    default: return nullptr;
  }
}
std::string vramInventory(int top) {
  struct T { size_t b; int w, h, d, lv; GLint f, tgt; GLuint id; };
  std::vector<T> tex; size_t tb = 0, tcomp = 0, bb = 0, rb = 0; int nb = 0, nr = 0;
  const GLuint scan = std::getenv("NMK_VRAM_SCAN") ? (GLuint)std::atol(std::getenv("NMK_VRAM_SCAN")) : 300000u;
  for (GLuint id = 1; id < scan; ++id) {
    if (glIsTexture(id)) {
      GLint tgt = 0, lv = 1, f = 0, w = 0, h = 0, d = 0, comp = 0; glGetTextureParameteriv(id, GL_TEXTURE_TARGET, &tgt); glGetTextureParameteriv(id, GL_TEXTURE_IMMUTABLE_LEVELS, &lv);
      if (tgt == GL_TEXTURE_BUFFER) continue;   // its buffer is counted below
      glGetTextureLevelParameteriv(id, 0, GL_TEXTURE_INTERNAL_FORMAT, &f); glGetTextureLevelParameteriv(id, 0, GL_TEXTURE_WIDTH, &w); glGetTextureLevelParameteriv(id, 0, GL_TEXTURE_HEIGHT, &h); glGetTextureLevelParameteriv(id, 0, GL_TEXTURE_DEPTH, &d);
      size_t b = 0; const int faces = tgt == GL_TEXTURE_CUBE_MAP ? 6 : 1;
      for (int L = 0; L < std::max(1, lv); ++L) {
        GLint c = 0, sz = 0, lw = 0, lh = 0, ld = 0, rs = 0, gs = 0, bs = 0, as = 0, ds = 0, ss = 0, smp = 0; glGetTextureLevelParameteriv(id, L, GL_TEXTURE_COMPRESSED, &c);
        if (c) { glGetTextureLevelParameteriv(id, L, GL_TEXTURE_COMPRESSED_IMAGE_SIZE, &sz); b += (size_t)sz * (tgt == GL_TEXTURE_CUBE_MAP ? 6 : 1); comp = 1; continue; }   // cube: one face's size
        glGetTextureLevelParameteriv(id, L, GL_TEXTURE_WIDTH, &lw); glGetTextureLevelParameteriv(id, L, GL_TEXTURE_HEIGHT, &lh); glGetTextureLevelParameteriv(id, L, GL_TEXTURE_DEPTH, &ld);
        glGetTextureLevelParameteriv(id, L, GL_TEXTURE_RED_SIZE, &rs); glGetTextureLevelParameteriv(id, L, GL_TEXTURE_GREEN_SIZE, &gs); glGetTextureLevelParameteriv(id, L, GL_TEXTURE_BLUE_SIZE, &bs);
        glGetTextureLevelParameteriv(id, L, GL_TEXTURE_ALPHA_SIZE, &as); glGetTextureLevelParameteriv(id, L, GL_TEXTURE_DEPTH_SIZE, &ds); glGetTextureLevelParameteriv(id, L, GL_TEXTURE_STENCIL_SIZE, &ss); glGetTextureLevelParameteriv(id, L, GL_TEXTURE_SAMPLES, &smp);
        const int bits = rs + gs + bs + as + ds + ss; b += (size_t)lw * std::max(1, lh) * std::max(1, ld) * faces * std::max(1, smp) * ((bits + 7) / 8);
      }
      tb += b; if (comp) tcomp += b; tex.push_back({b, w, h, d, lv, f, tgt, id});
    } else if (glIsBuffer(id)) { GLint64 s = 0; glGetNamedBufferParameteri64v(id, GL_BUFFER_SIZE, &s); bb += (size_t)s; ++nb; }
    else if (glIsRenderbuffer(id)) {
      GLint w = 0, h = 0, smp = 0, rs = 0, gs = 0, bs = 0, as = 0, ds = 0, ss = 0;
      glGetNamedRenderbufferParameteriv(id, GL_RENDERBUFFER_WIDTH, &w); glGetNamedRenderbufferParameteriv(id, GL_RENDERBUFFER_HEIGHT, &h); glGetNamedRenderbufferParameteriv(id, GL_RENDERBUFFER_SAMPLES, &smp);
      glGetNamedRenderbufferParameteriv(id, GL_RENDERBUFFER_RED_SIZE, &rs); glGetNamedRenderbufferParameteriv(id, GL_RENDERBUFFER_GREEN_SIZE, &gs); glGetNamedRenderbufferParameteriv(id, GL_RENDERBUFFER_BLUE_SIZE, &bs);
      glGetNamedRenderbufferParameteriv(id, GL_RENDERBUFFER_ALPHA_SIZE, &as); glGetNamedRenderbufferParameteriv(id, GL_RENDERBUFFER_DEPTH_SIZE, &ds); glGetNamedRenderbufferParameteriv(id, GL_RENDERBUFFER_STENCIL_SIZE, &ss);
      rb += (size_t)w * h * std::max(1, smp) * ((rs + gs + bs + as + ds + ss + 7) / 8); ++nr;
    }
  }
  while (glGetError() != GL_NO_ERROR) {}
  std::sort(tex.begin(), tex.end(), [](const T& a, const T& b) { return a.b > b.b; });
  const double M = 1048576.0; char b[512];
  std::snprintf(b, sizeof b, "[vram] inventory: %zu textures %.0f MB (%.0f MB block-compressed), %d buffers %.0f MB, %d renderbuffers %.0f MB; total %.0f MB",
                tex.size(), tb / M, tcomp / M, nb, bb / M, nr, rb / M, (tb + bb + rb) / M);
  std::string out = b;
  for (int k = 0; k < std::min<int>(top, (int)tex.size()); ++k) {
    const T& t = tex[k]; const char* fn = fmtName(t.f); char fb[16]; if (!fn) { std::snprintf(fb, sizeof fb, "0x%X", (unsigned)t.f); fn = fb; }
    std::snprintf(b, sizeof b, "\n[vram]   %2d. %7.1f MB  %dx%d%s%s %s, %d mips (id %u)", k + 1, t.b / M, t.w, t.h, t.d > 1 ? "x" : "", t.d > 1 ? std::to_string(t.d).c_str() : "", fn, t.lv, t.id);
    out += b;
  }
  return out;
}
bool gpuVertexPackCheck(std::string& why);   // assets.cpp
fs::path gShot;
bool memProbeSelfTest(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--mem-probe") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) { if (!gShot.empty()) { a.screenshot(gShot); gShot.clear(); } return true; }
  t += dt; ++frames;
  static float tick = 0; tick += dt;
  if (tick >= 10 && phase == 8) { tick = 0; ProcMem m = processMemory(); logf("[mem]   t %4.0f s %s: commit %6.0f MB  working set %6.0f MB", t, phase == 0 ? (mainMenuActive() ? "menu" : "startup (no menu)") : "match", m.PrivateBytes / 1048576.0, m.WorkingSetSize / 1048576.0); }
  auto next = [&]() { ++phase; t = 0; };
  auto load = [&](const char* id) { const TemplateInfo* ti = a.findTemplate(id); if (ti) a.newMap(*ti, id); else logf("[mem] no template %s", id); cpu0 = cpuSec(); wall0 = wall(); frames = 0; };
  switch (phase) {
    case 0: { static double w0 = -1; if (w0 < 0) { w0 = wall(); cpu0 = cpuSec(); wall0 = w0; frames = 0; mainMenuOpen(a); }   // the real menu (self-tests skip it); wall clock: the menu may hold dt
      static double lastTick = 0; t = (float)(wall() - w0);
      if (t - lastTick >= 10) { lastTick = t; ProcMem m = processMemory(); logf("[mem]   t %4.0f s %s: commit %6.0f MB  working set %6.0f MB", t, mainMenuActive() ? "menu" : "no menu", m.PrivateBytes / 1048576.0, m.WorkingSetSize / 1048576.0); } } if (t > (std::getenv("NMK_MEM_MENU_S") ? std::atof(std::getenv("NMK_MEM_MENU_S")) : 10.0)) { report("main menu idle"); mainMenuGo(a, 0); next(); if (std::getenv("NMK_MEM_MENU_ONLY")) phase = 99; if (std::getenv("NMK_MEM_CYCLE")) phase = 200; } break;
    case 1: if (std::getenv("NMK_MEM_PALETTE")) {   // MEM2: browse 40 uncached tiles as the palette would (render, then release)
        extern void assetThumbOnDemand(App&, AssetDef&, int&);
        ProcMem m0 = processMemory(); int shown = 0;
        for (auto& ap : a.lib.assets) { if (ap->vehicle || ap->built || ap->thumbnail) continue; int b = 2; assetThumbOnDemand(a, *ap, b); if (++shown >= 40) break; }
        int built = 0; for (auto& ap : a.lib.assets) built += ap->built;
        ProcMem m1 = processMemory();
        logf("[mem] palette: %d tiles shown, commit %.0f -> %.0f MB, %d assets built after (released ones are not)", shown, m0.PrivateBytes / 1048576.0, m1.PrivateBytes / 1048576.0, built);
      } [[fallthrough]];
    case 100: { std::string why; logf("[mem] GpuVertex packing check: %s", gpuVertexPackCheck(why) ? "PASS" : ("FAIL " + why).c_str()); load("blank_slate"); next(); } break;
    case 2: if (t > 5) { report("small map (blank slate)"); next(); } break;
    case 3: load("chateau_town"); next(); break;
    case 4: if (t > 4.5f && t - dt <= 4.5f) { fs::path d = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(d, ec); gShot = d / "mem1_large.png"; }
      if (t > 5) { report("large map (chateau_town)"); cpuCopies(a); next(); } break;
    case 5: load("chateau_town"); next(); break;
    case 6: if (t > 5) { report("large map again"); next(); } break;
    case 7: { std::string err; if (!startMatch(a, 20, err)) { logf("[mem] match: %s", err.c_str()); phase = 9; } else next(); t = 0; } break;
    case 8: if (t > 60) { report("match, 20 bots, 60 s"); a.exitExplore(); next(); } break;
    case 9: load("blank_slate"); next(); break;   // MEM2: the old map's assets go with it
    case 10: if (t > 5) { report("back to a small map"); next(); } break;
    case 11: load("chateau_town"); next(); break;
    case 12: if (t > 4.5f && t - dt <= 4.5f) { fs::path d = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(d, ec); gShot = d / "mem2_reload.png"; }
      if (t > 5) { report("large map rebuilt"); next(); } break;
    case 200: {   // MEM3: NMK_MEM_CYCLE=n map changes alternating Village, Sun Fort, Outpost Woods (as played: openMap)
      static int k = 0; static const char* maps[3] = {"village_assault", "sd_desert", "outpost_woods"};
      if (k > 0 && t > 5.5f && t - dt <= 5.5f && std::getenv("NMK_MEM_SHOTS")) { fs::path d = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(d, ec); gShot = d / ("mem3_" + std::to_string(k) + "_" + maps[(k - 1) % 3] + ".png"); }
      if (k > 0 && t < 6) break;   // 6 s on each map: its lazy builds and first frames
      if (k > 0) { char b[64]; std::snprintf(b, sizeof b, "cycle %2d %s", k, maps[(k - 1) % 3]); report(b); breakdown(); }
      if (k >= std::atoi(std::getenv("NMK_MEM_CYCLE"))) { phase = 99; break; }
      a.openMap(contentDir() / "templates" / (std::string(maps[k % 3]) + ".nmap.json")); ++k; t = 0; cpu0 = cpuSec(); wall0 = wall(); frames = 0; } break;
    default: { ProcMem m = processMemory(); logf("[mem] done (peak working set %.0f MB)", m.PeakWorkingSetSize / 1048576.0); a.selftest->exitCode = 0; a.selftest->done = true; } break;
  }
  return true;
}
