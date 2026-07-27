#pragma once

// ============================================================================
// Module: lua_tonumber_fast.h
// Description: Accelerates Lua runtime calls in `lua_tonumber_fast.h`.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================










#include <cstdint>

// Lua tonumber fast path optimization
// Hooks lua_tonumber (0x0084E0E0) to skip type conversion for numbers
bool InstallLuaToNumberFast();

// Statistics
void GetLuaToNumberStats(uint64_t* fast_path, uint64_t* slow_path);
