#pragma once

// ============================================================================
// Module: lua_precall_cache.h
// Description: Accelerates Lua runtime calls in `lua_precall_cache.h`. Caches structures to bypass parser overhead.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================









bool InstallLuaPrecallCache();
void PrecallCache_Invalidate();
