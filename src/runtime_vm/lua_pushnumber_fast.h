#pragma once

// ============================================================================
// Module: lua_pushnumber_fast.h
// Description: Accelerates Lua runtime calls in `lua_pushnumber_fast.h`.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================










bool InstallLuaPushNumberFast();
void ShutdownLuaPushNumberFast();