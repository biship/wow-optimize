#pragma once

// ============================================================================
// Module: lua_numconv_fast.h
// Description: Accelerates Lua runtime calls in `lua_numconv_fast.h`.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================










bool InstallLuaNumConvFast();
void ShutdownLuaNumConvFast();
