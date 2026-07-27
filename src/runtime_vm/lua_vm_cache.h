#pragma once

// ============================================================================
// Module: lua_vm_cache.h
// Description: Accelerates Lua runtime calls in `lua_vm_cache.h`. Caches structures to bypass parser overhead.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================










bool InstallLuaVMCache();
void ClearTableCache();
void GetTableCacheStats(long long* hits, long long* misses);
