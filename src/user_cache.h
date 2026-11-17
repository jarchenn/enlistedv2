#pragma once
// Per-user disk caches (thumbs, furnish, texbc): one location, recency kept by touching a file on every hit, a size cap
// pruned oldest-first in the background at startup, and a clear for Settings > Interface (user_cache.cpp)
#include "common.h"
fs::path userCacheDir(const char* sub, const char* envOverride);   // envOverride set: that folder; else %LOCALAPPDATA%/BattleFoundry/<sub>
void userCacheTouch(const fs::path& f);                              // a hit: the file becomes the most recently used
void userCachePruneAsync();                                          // startup: trim to the cap (NMK_CACHE_CAP_MB, default 2048) on a thread
uint64_t userCacheBytes(bool rescan);                                 // their total size (the last scan; rescan: count now)
uint64_t userCacheCapBytes();
void userCacheClear();                                                // every cached file (each regenerates when next needed)
