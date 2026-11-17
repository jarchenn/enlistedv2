// see user_cache.h
#include "user_cache.h"
#include <atomic>
#include <thread>
#include <algorithm>

namespace {
const char* kSubs[3][2] = {{"thumbs", "NMK_THUMB_CACHE"}, {"furnish", "NMK_FURNISH_CACHE"}, {"texbc", "NMK_TEXBC_CACHE"}};
std::atomic<uint64_t> gBytes{0};
struct F { fs::path p; uintmax_t size; fs::file_time_type t; };
std::vector<F> scan() {
  std::vector<F> v; std::error_code ec;
  for (auto& s : kSubs) {
    fs::path d = userCacheDir(s[0], s[1]);
    for (auto it = fs::directory_iterator(d, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
      if (!it->is_regular_file(ec)) continue;
      std::error_code e2; uintmax_t sz = it->file_size(e2); auto t = it->last_write_time(e2);
      if (!e2) v.push_back({it->path(), sz, t});
    }
  }
  return v;
}
}

fs::path userCacheDir(const char* sub, const char* envOverride) {
  if (envOverride) if (const char* e = std::getenv(envOverride)) if (*e) return fs::path(e);
#ifdef _WIN32
  if (const char* e = std::getenv("LOCALAPPDATA")) if (*e) return fs::path(e) / "BattleFoundry" / sub;
#else
  if (const char* e = std::getenv("HOME")) if (*e) return fs::path(e) / ".cache" / "BattleFoundry" / sub;
#endif
  return exeDir() / "cache" / sub;
}
void userCacheTouch(const fs::path& f) { std::error_code ec; fs::last_write_time(f, fs::file_time_type::clock::now(), ec); }
uint64_t userCacheCapBytes() {
  static const uint64_t cap = [] { const char* e = std::getenv("NMK_CACHE_CAP_MB"); long long mb = e && *e ? std::atoll(e) : 2048; return (uint64_t)std::max(64LL, mb) << 20; }();
  return cap;
}
void userCachePruneAsync() {
  std::thread([] {
    std::vector<F> v = scan();
    uint64_t total = 0; for (auto& f : v) total += f.size;
    const uint64_t cap = userCacheCapBytes();
    int removed = 0; uint64_t freed = 0;
    if (total > cap) {   // least recently used first, down to 90 % of the cap (a little headroom before the next prune)
      std::sort(v.begin(), v.end(), [](const F& a, const F& b) { return a.t < b.t; });
      for (auto& f : v) { if (total <= cap / 10 * 9) break; std::error_code ec; if (fs::remove(f.p, ec)) { total -= f.size; freed += f.size; ++removed; } }
    }
    gBytes = total;
    logf("caches: %.0f MB in %zu files (cap %.0f MB)%s", total / 1048576.0, v.size() - removed, cap / 1048576.0, removed ? (", pruned " + std::to_string(removed) + " least recently used files, " + std::to_string(freed >> 20) + " MB").c_str() : "");
  }).detach();
}
uint64_t userCacheBytes(bool rescan) {
  if (rescan) { uint64_t t = 0; for (auto& f : scan()) t += f.size; gBytes = t; }
  return gBytes;
}
void userCacheClear() {
  std::error_code ec;
  for (auto& s : kSubs) { fs::path d = userCacheDir(s[0], s[1]); for (auto it = fs::directory_iterator(d, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) { std::error_code e2; if (it->is_regular_file(e2)) fs::remove(it->path(), e2); } }
  gBytes = 0;
  logf("caches: cleared");
}
