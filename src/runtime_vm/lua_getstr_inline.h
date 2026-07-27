#pragma once

// ============================================================================
// Module: lua_getstr_inline.h
// Description: Accelerates Lua runtime calls in `lua_getstr_inline.h`.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================










bool InstallLuaGetStrInline();
void UninstallLuaGetStrInline();
void InvalidateLuaGetStrInlineCache();  // zeroes the entire 16384-entry bucket cache
