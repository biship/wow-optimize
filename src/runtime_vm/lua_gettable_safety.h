#pragma once

// ============================================================================
// Module: lua_gettable_safety.h
// Description: Accelerates Lua runtime calls in `lua_gettable_safety.h`.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================









// lua_gettable_safety.h - Crash fix for sub_85BC10 TValue corruption

bool InstallLuaGetTableSafety();
void UninstallLuaGetTableSafety();
LONG64 GetTableSafety_GetBlockedCount();
LONG64 GetTableSafety_GetTotalCount();