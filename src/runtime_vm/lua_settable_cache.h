#pragma once

// ============================================================================
// Module: lua_settable_cache.h
// Description: Accelerates Lua runtime calls in `lua_settable_cache.h`. Caches structures to bypass parser overhead.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================










bool InstallLuaSetTableCache();
void ShutdownLuaSetTableCache();
void ClearLuaSetTableCache();