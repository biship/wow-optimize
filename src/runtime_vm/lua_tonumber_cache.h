#pragma once

// ============================================================================
// Module: lua_tonumber_cache.h
// Description: Accelerates Lua runtime calls in `lua_tonumber_cache.h`. Caches structures to bypass parser overhead.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================










bool InstallLuaToNumberCache();
void UninstallLuaToNumberCache();
