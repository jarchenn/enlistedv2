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
  std::snprintf(b, sizeof b, "[vram] GPU %s in use %.0f of %.0f MB, evictions %d (%.0f MB); characters: %d instances %.1f MB, %d types %.1f MB geometry + %.1f MB textures",
                ded ? "(NVX)" : "(no NVX)", (tot - avail) / 1024.0, ded / 1024.0, evN, evKB / 1024.0, gCharBytes.nInst, gCharBytes.inst / M, gCharBytes.nType, gCharBytes.type / M, gCharBytes.tex / M);
  return b;
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
