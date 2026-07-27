#pragma once

// ============================================================================
// Module: lua_rawgeti_inline.h
// Description: Accelerates Lua runtime calls in `lua_rawgeti_inline.h`.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================










bool InstallLuaRawGetIInline();
void UninstallLuaRawGetIInline();
void ClearRawGetIInlineCache();  // zeroes the entire 8192-entry bucket cache
