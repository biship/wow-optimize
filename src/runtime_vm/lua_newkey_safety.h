#pragma once

// ============================================================================
// Module: lua_newkey_safety.h
// Description: Accelerates Lua runtime calls in `lua_newkey_safety.h`.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================










bool InstallLuaNewKeySafety();
void UninstallLuaNewKeySafety();
