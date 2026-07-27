#pragma once

// ============================================================================
// Module: lua_gettable_cache.h
// Description: Accelerates Lua runtime calls in `lua_gettable_cache.h`. Caches structures to bypass parser overhead.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================










// luaV_gettable Fast Path Cache
// Hooks sub_857250 to cache table+key lookups
bool InstallLuaGetTableCache();
void ShutdownLuaGetTableCache();
