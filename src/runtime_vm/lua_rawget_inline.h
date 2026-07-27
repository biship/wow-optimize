#pragma once

// ============================================================================
// Module: lua_rawget_inline.h
// Description: Accelerates Lua runtime calls in `lua_rawget_inline.h`.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================









bool InstallLuaRawGetInline();
void UninstallLuaRawGetInline();
