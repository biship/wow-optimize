#pragma once

// ============================================================================
// Module: lua_pushvalue_fast.h
// Description: Accelerates Lua runtime calls in `lua_pushvalue_fast.h`.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================










bool InstallLuaPushValueFast(void);
void ShutdownLuaPushValueFast(void);