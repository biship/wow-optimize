#pragma once

// ============================================================================
// Module: lua_checknumber_cache.h
// Description: Accelerates Lua runtime calls in `lua_checknumber_cache.h`. Caches structures to bypass parser overhead.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================










bool InstallLuaCheckNumberCache();
void UninstallLuaCheckNumberCache();
