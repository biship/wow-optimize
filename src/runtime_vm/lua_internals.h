#pragma once

// ============================================================================
// Module: lua_internals.h
// Description: Accelerates Lua runtime calls in `lua_internals.h`.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================









#ifndef LUA_INTERNALS_H
#define LUA_INTERNALS_H

#include <windows.h>

namespace LuaInternals {

bool Init();
void Shutdown();
void OnGCStep();
void InvalidateCache();

struct Stats {
    long concatFastHits;
    long concatFallbacks;
    bool active;
};

Stats GetStats();

} // namespace LuaInternals

#endif