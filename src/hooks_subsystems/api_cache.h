#pragma once

// ============================================================================
// Module: api_cache.h
// ============================================================================









#ifndef API_CACHE_H
#define API_CACHE_H

#include <windows.h>

struct lua_State;

namespace ApiCache {

bool Init();
void Shutdown();
void OnNewFrame();
void ClearCache();

struct Stats {
    long itemHits;
    long itemMisses;
    long spellHits;
    long spellMisses;
    bool active;
};

Stats GetStats();

} // namespace ApiCache

#endif