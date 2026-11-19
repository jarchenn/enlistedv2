#pragma once
// Per-user disk caches (thumbs, furnish, texbc): one location, recency kept by touching a file on every hit, a size cap
// pruned oldest-first in the background at startup, and a clear for Settings > Interface (user_cache.cpp)
#include "common.h"
fs::path userCacheDir(const char* sub, const char* envOverride);   // envOverride set: that folder; else %LOCALAPPDATA%/BattleFoundry/<sub>
void userCacheTouch(const fs::path& f);                              // a hit: the file becomes the most recently used
std::string userCacheContentStamp(const fs::path& f);               // CACHE2: size + a hash of its first and last 64 KB ("" if unreadable):
                                                                    // the same source in another build folder or exe copy keys the same
enum UserCache { UC_THUMBS = 0, UC_FURNISH, UC_TEXBC, UC_COUNT };   // CACHE2: each with its own cap
const char* userCacheName(int c);
void userCachePruneAsync();                                          // after loading: each cache back under its cap (unused 30 days first) on a thread
uint64_t userCacheBytes(int c, bool rescan);                          // one cache's size (the last scan; rescan: count now)
uint64_t userCacheCapBytes(int c);                                    // NMK_CACHE_CAP_<THUMBS|FURNISH|TEXBC>_MB; 300 / 300 / 6144 MB (TX3: 2K sets, was 3072)
void userCacheClear();                                                // every cached file (each regenerates when next needed)
