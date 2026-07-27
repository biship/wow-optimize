#pragma once

// ============================================================================
// Module: lua_getn_fast.h
// Description: Accelerates Lua runtime calls in `lua_getn_fast.h`.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================









bool InstallLuaGetnFast();
void UninstallLuaGetnFast();
