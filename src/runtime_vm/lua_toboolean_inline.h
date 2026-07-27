#pragma once

// ============================================================================
// Module: lua_toboolean_inline.h
// Description: Accelerates Lua runtime calls in `lua_toboolean_inline.h`.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================









bool InstallLuaTobooleanInline();
void UninstallLuaTobooleanInline();