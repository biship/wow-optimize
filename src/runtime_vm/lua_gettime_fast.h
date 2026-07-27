// ============================================================================
// Module: lua_gettime_fast.h
// Description: Declarations for accelerating Lua GetTime via frame caching.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================

#pragma once

bool InstallLuaGetTimeFast();
void LuaGetTimeFast_NewFrame();
